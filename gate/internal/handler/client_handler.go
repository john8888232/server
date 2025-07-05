package handler

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/base64"
	"encoding/binary"
	"gate/internal/config"
	"gate/internal/model"
	"gate/internal/service"
	"gate/internal/util"
	"gate/proto"
	"net"
	"net/http"
	"strings"
	"time"

	"github.com/gorilla/websocket"
	protobuf "google.golang.org/protobuf/proto"
)

var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	CheckOrigin: func(r *http.Request) bool {
		return true // 跨域，允许所有来源
	},
}

type ClientHandler struct {
	cfg           *config.Config
	clientManager *model.ClientManager
	gameServerMgr *model.GameServerManager
	redisService  *service.RedisService
}

func NewClientHandler(cfg *config.Config, cm *model.ClientManager, gsm *model.GameServerManager, rs *service.RedisService) *ClientHandler {
	return &ClientHandler{
		cfg:           cfg,
		clientManager: cm,
		gameServerMgr: gsm,
		redisService:  rs,
	}
}

func (ch *ClientHandler) ServeWS(w http.ResponseWriter, r *http.Request) {
	// 升级为 WebSocket 连接
	wsConn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		util.Logger.Errorf("Failed to upgrade to WebSocket: %v", err)
		return
	}

	// 生成唯一的 SessionID
	sessionID := generateSessionID()
	clientIP := getClientIP(r)
	client := &model.Client{
		Conn:      wsConn,
		SessionID: sessionID,
	}

	// 添加到客户端管理器
	ch.clientManager.AddClient(sessionID, client)
	util.Logger.Infof("New client connected, SessionID: %s, IP: %s, Total Clients: %d", sessionID, clientIP, ch.clientManager.Len())

	// 处理消息
	ch.handleClientMessages(client, clientIP)
}

// handleClientMessages 处理客户端消息
func (ch *ClientHandler) handleClientMessages(client *model.Client, clientIP string) {
	defer func() {
		client.Conn.Close()
		ch.clientManager.RemoveClient(client.SessionID)
		if client.GameServer != nil {
			// 通知 GameServer 客户端断开
			ch.sendConnCloseToGameServer(client)
		}
		util.Logger.Infof("Client disconnected, SessionID: %s", client.SessionID)
	}()

	for {
		// 读取 WebSocket 消息
		_, data, err := client.Conn.ReadMessage()
		if err != nil {
			util.Logger.Errorf("Failed to read from client %s: %v", client.SessionID, err)
			return
		}

		util.Logger.Infof("Received raw data from client %s (length: %d bytes)", client.SessionID, len(data))
		util.Logger.Infof("Raw data hex: %x", data)
		util.Logger.Infof("Client IP: %s", clientIP)

		// 使用 Unpack 解包消息
		buffer := bytes.NewBuffer(data)
		msgID, sessionID, msgData, err := util.Unpack(buffer)
		if err != nil {
			util.Logger.Warnf("Failed to unpack message from client %s: %v", client.SessionID, err)
			util.Logger.Warnf("Failed to unpack data: %x", data)
			continue
		}

		util.Logger.Infof("Unpack successful - Client: %s, MsgID: 0x%X, SessionID: %s, payload length: %d",
			client.SessionID, msgID, sessionID, len(msgData))

		if msgID == 0x11001 { // LoginReq
			util.Logger.Infof("Processing login request...")
			var loginReq proto.LoginReq
			if err := protobuf.Unmarshal(msgData, &loginReq); err != nil {
				util.Logger.Errorf("Failed to unmarshal LoginReq from client %s: %v", client.SessionID, err)
				util.Logger.Errorf("Unable to parse login data: %x", msgData)
				continue
			}

			util.Logger.Infof("Login request content:")
			util.Logger.Infof("  Username: %s", loginReq.Loginname)
			util.Logger.Infof("  Token: %s", loginReq.Token)
			util.Logger.Infof("  Game Type: %s", loginReq.GameType)
			util.Logger.Infof("  Version: %d", loginReq.Version)

			ch.handleLoginRequest(client, &loginReq, clientIP)
		} else {
			util.Logger.Infof("Forwarding message to GameServer - MsgID: 0x%X", msgID)
			// 其他消息直接转发到 GameServer
			if client.GameServer == nil {
				util.Logger.Warnf("No GameServer associated with client %s for msgID %d", client.SessionID, msgID)
				continue
			}
			// 打包并转发到 GameServer (TCP)
			tcpData := util.Pack(msgID, client.SessionID, msgData)
			util.Logger.Infof("TCP data sending to GameServer (length: %d): %x", len(tcpData), tcpData)
			_, err := client.GameServer.Conn.Write(tcpData)
			if err != nil {
				util.Logger.Errorf("Failed to forward message to GameServer for client %s: %v", client.SessionID, err)
			} else {
				util.Logger.Infof("Message successfully forwarded to GameServer")
			}
		}
	}
}

// handleLoginRequest 处理登录请求
func (ch *ClientHandler) handleLoginRequest(client *model.Client, loginReq *proto.LoginReq, clientIP string) {
	ctx := context.Background()

	// 检查玩家是否已登录 (顶号检测)
	gameServerID, err := ch.redisService.GetPlayerGameServer(ctx, loginReq.Loginname)
	if err != nil {
		util.Logger.Errorf("Failed to check player login status for user %s: %v", loginReq.Loginname, err)
		ch.sendLoginResponse(client, loginReq.Loginname, 1, "Internal server error")
		return
	}

	var targetGameServer *model.GameServer
	if gameServerID != "" {
		// 玩家已登录，将登录请求转发到相应的游戏服
		util.Logger.Warnf("User %s already logged in on GameServer %s", loginReq.Loginname, gameServerID)
		targetGameServer = ch.gameServerMgr.GetServerByID(gameServerID)
		if targetGameServer == nil {
			util.Logger.Errorf("GameServer %s not found for user %s, selecting new server", gameServerID, loginReq.Loginname)
			gameServerID = ""
		}
	}

	if gameServerID == "" {
		// 玩家未登录或游戏服不存在，根据 GameType 和负载选择 GameServer
		gameType := loginReq.GameType
		if gameType == "" {
			util.Logger.Errorf("GameType is empty for user %s", loginReq.Loginname)
			ch.sendLoginResponse(client, loginReq.Loginname, 2, "GameType is empty")
			return
		}
		targetGameServer = ch.gameServerMgr.SelectServerByLoad(gameType)
		if targetGameServer == nil {
			util.Logger.Warnf("No available GameServer for GameType %s", loginReq.GameType)
			ch.sendLoginResponse(client, loginReq.Loginname, 3, "No game server available")
			return
		}
	}

	// 连接到 GameServer (如果未连接)
	if targetGameServer.Conn == nil {
		if err := service.ConnectToGameServer(targetGameServer); err != nil {
			util.Logger.Errorf("Failed to connect to GameServer %s:%d: %v", targetGameServer.IP, targetGameServer.Port, err)
			ch.sendLoginResponse(client, loginReq.Loginname, 4, "Failed to connect to game server")
			return
		}
	}

	// 更新 LoginReq，添加客户端 IP 和 SessionID
	loginReq.ClientIp = clientIP

	// 序列化并转发到 GameServer
	data, err := protobuf.Marshal(loginReq)
	if err != nil {
		util.Logger.Errorf("Failed to marshal LoginReq for user %s: %v", loginReq.Loginname, err)
		ch.sendLoginResponse(client, loginReq.Loginname, 5, "Internal server error")
		return
	}

	util.Logger.Infof("Preparing to send login request to GameServer %s:%d", targetGameServer.IP, targetGameServer.Port)
	util.Logger.Infof("Serialized login data (length: %d): %x", len(data), data)

	tcpData := util.Pack(0x11001, client.SessionID, data)
	util.Logger.Infof("Packed TCP data (length: %d): %x", len(tcpData), tcpData)

	// 详细分析包结构
	util.Logger.Infof("TCP Packet Analysis:")
	util.Logger.Infof("  Length field: %x (%d bytes - excluding length field itself)", tcpData[0:4], binary.BigEndian.Uint32(tcpData[0:4]))
	util.Logger.Infof("  MsgID field: %x (0x%X)", tcpData[4:8], binary.BigEndian.Uint32(tcpData[4:8]))
	util.Logger.Infof("  SessionID field: %x ('%s')", tcpData[8:40], string(bytes.TrimRight(tcpData[8:40], "\x00")))
	util.Logger.Infof("  Protobuf data: %x (%d bytes)", tcpData[40:], len(tcpData[40:]))
	util.Logger.Infof("  Expected total: 4(length) + 4(msgid) + 32(session) + %d(data) = %d bytes", len(data), 4+4+32+len(data))

	// 发送到 GameServer
	n, err := targetGameServer.Conn.Write(tcpData)
	if err != nil {
		util.Logger.Errorf("Failed to send LoginReq to GameServer for user %s: %v", loginReq.Loginname, err)
		ch.sendLoginResponse(client, loginReq.Loginname, 6, "Failed to communicate with game server")
		return
	}

	util.Logger.Infof("Successfully sent %d bytes to GameServer %s", n, targetGameServer.ID)

	// 暂时关联 GameServer，等待登录响应确认
	client.GameServer = targetGameServer
	util.Logger.Infof("Client %s associated with GameServer %s, waiting for login response...", client.SessionID, targetGameServer.ID)
}

// sendLoginResponse 发送登录响应给客户端
func (ch *ClientHandler) sendLoginResponse(client *model.Client, loginname string, code int32, message string) {
	resp := &proto.LoginResp{
		Loginname: loginname,
		Code:      code,
		Message:   message,
	}
	data, err := protobuf.Marshal(resp)
	if err != nil {
		util.Logger.Errorf("Failed to marshal LoginResp for user %s: %v", loginname, err)
		return
	}

	// 使用 util.Pack 打包消息
	msgData := util.Pack(0x11002, client.SessionID, data)

	// 发送消息到客户端
	err = client.Conn.WriteMessage(websocket.BinaryMessage, msgData)
	if err != nil {
		util.Logger.Errorf("Failed to send LoginResp to client %s: %v", client.SessionID, err)
	}
}

// generateSessionID 生成唯一的 SessionID
func generateSessionID() string {
	// 分配24字节缓冲区（编码后为32字符）
	buf := make([]byte, 24)

	// 前8字节存储纳秒时间戳（截取高有效位）
	binary.BigEndian.PutUint64(buf[:8], uint64(time.Now().UnixNano()))

	// 后16字节填充加密随机数
	_, _ = rand.Read(buf[8:])

	// 转换为URL安全的Base64字符串
	return base64.RawURLEncoding.EncodeToString(buf)
}

// getClientIP 获取客户端真实 IP
func getClientIP(r *http.Request) string {
	if ip := r.Header.Get("X-Forwarded-For"); ip != "" {
		return strings.Split(ip, ",")[0]
	}
	if ip := r.Header.Get("X-Real-IP"); ip != "" {
		return ip
	}
	host, _, _ := net.SplitHostPort(r.RemoteAddr)
	return host
}

// sendConnCloseToGameServer 向游戏服务器发送客户端断开连接通知
func (ch *ClientHandler) sendConnCloseToGameServer(client *model.Client) {
	if client.GameServer == nil || client.GameServer.Conn == nil {
		return
	}

	// 创建ConnClose消息
	connClose := &proto.ConnClose{}
	data, err := protobuf.Marshal(connClose)
	if err != nil {
		util.Logger.Errorf("Failed to marshal ConnClose for client %s: %v", client.SessionID, err)
		return
	}

	// 打包并发送到 GameServer (使用消息ID 0x10001)
	tcpData := util.Pack(0x10001, client.SessionID, data)
	_, err = client.GameServer.Conn.Write(tcpData)
	if err != nil {
		util.Logger.Errorf("Failed to send ConnClose to GameServer for client %s: %v", client.SessionID, err)
	} else {
		util.Logger.Infof("Sent ConnClose notification to GameServer for client %s", client.SessionID)
	}
}
