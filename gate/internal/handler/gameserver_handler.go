package handler

import (
	"bytes"
	"gate/internal/config"
	"gate/internal/model"
	"gate/internal/util"
	"gate/proto"

	"github.com/gorilla/websocket"
	protobuf "google.golang.org/protobuf/proto"
)

type GameServerHandler struct {
	cfg           *config.Config
	clientManager *model.ClientManager
}

func NewGameServerHandler(cfg *config.Config, cm *model.ClientManager) *GameServerHandler {
	return &GameServerHandler{
		cfg:           cfg,
		clientManager: cm,
	}
}

// HandleGameServerMessage 处理从 GameServer 接收到的消息
func (gsh *GameServerHandler) HandleGameServerMessage(gs *model.GameServer) {
	// 确保连接已建立
	if gs.Conn == nil {
		if err := gs.Connect(); err != nil {
			util.Logger.Errorf("Failed to connect to GameServer %s:%d: %v", gs.IP, gs.Port, err)
			return
		}
	}

	defer func() {
		if gs.Conn != nil {
			gs.Conn.Close()
			gs.Conn = nil
		}
		util.Logger.Warnf("Disconnected from GameServer %s:%d", gs.IP, gs.Port)
	}()

	buffer := bytes.NewBuffer(nil)
	tmpBuf := make([]byte, 1024)

	for {
		n, err := gs.Conn.Read(tmpBuf)
		if err != nil {
			util.Logger.Errorf("Failed to read from GameServer %s:%d: %v", gs.IP, gs.Port, err)
			return
		}
		buffer.Write(tmpBuf[:n])

		for {
			msgID, sessionID, data, err := util.Unpack(buffer)
			if err != nil {
				break // 数据不足
			}

			util.Logger.Infof("Received message from GameServer %s - ID: 0x%X, session: %s, data length: %d",
				gs.ID, msgID, sessionID, len(data))
			util.Logger.Infof("Data content: %x", data)

			if msgID == 0x11002 { // LoginResp
				util.Logger.Infof("Processing GameServer login response...")
				var loginResp proto.LoginResp
				if err := protobuf.Unmarshal(data, &loginResp); err != nil {
					util.Logger.Errorf("Failed to unmarshal LoginResp from GameServer %s:%d: %v", gs.IP, gs.Port, err)
					util.Logger.Errorf("Unable to parse response data: %x", data)
					continue
				}

				util.Logger.Infof("Login response content:")
				util.Logger.Infof("  Username: %s", loginResp.Loginname)
				util.Logger.Infof("  Response Code: %d", loginResp.Code)
				util.Logger.Infof("  Response Message: %s", loginResp.Message)
				if loginResp.Info != nil {
					util.Logger.Infof("  Player Info: username=%s, nickname=%s, balance=%.2f",
						loginResp.Info.Username, loginResp.Info.Nickname, loginResp.Info.Balance)
				}

				gsh.handleLoginResponse(gs, &loginResp, sessionID)
			} else if msgID == 0x10001 { // KickPlayerNotify
				util.Logger.Infof("Processing kick player notification...")
				var kickNotify proto.KickPlayerNotify
				if err := protobuf.Unmarshal(data, &kickNotify); err != nil {
					util.Logger.Errorf("Failed to unmarshal KickPlayerNotify from GameServer %s:%d: %v", gs.IP, gs.Port, err)
					continue
				}
				gsh.handleKickPlayerNotify(&kickNotify)
			} else {
				util.Logger.Infof("Forwarding other message to client - MsgID: 0x%X", msgID)
				gsh.forwardToClient(msgID, sessionID, data)
			}
		}
	}
}

// handleLoginResponse 处理登录响应
func (gsh *GameServerHandler) handleLoginResponse(gs *model.GameServer, loginResp *proto.LoginResp, sessionID string) {
	// 查找对应的客户端
	client, exists := gsh.clientManager.GetClient(sessionID)
	if !exists {
		util.Logger.Warnf("Client not found for SessionID %s in LoginResp", sessionID)
		return
	}

	// 发送响应给客户端 (WebSocket)
	data, err := protobuf.Marshal(loginResp)
	if err != nil {
		util.Logger.Errorf("Failed to marshal LoginResp for SessionID %s: %v", sessionID, err)
		return
	}

	util.Logger.Infof("Preparing to send login response to client %s", sessionID)
	util.Logger.Infof("Serialized response data (length: %d): %x", len(data), data)

	msgData := util.Pack(0x11002, sessionID, data)
	util.Logger.Infof("Packed WebSocket data (length: %d): %x", len(msgData), msgData)

	err = client.Conn.WriteMessage(websocket.BinaryMessage, msgData)
	if err != nil {
		util.Logger.Errorf("Failed to send LoginResp to client %s: %v", sessionID, err)
		return
	}

	util.Logger.Infof("Successfully sent login response to client %s", sessionID)

	if loginResp.Code == 0 { // 登录成功
		// 建立 SessionID -> GameServer 映射（直接设置到Client对象）
		client.GameServer = gs
		util.Logger.Infof("User %s login successful, bound to GameServer %s", loginResp.Loginname, gs.ID)
	} else { // 登录失败
		// 断开客户端连接
		client.Conn.Close()
		gsh.clientManager.RemoveClient(sessionID)
		util.Logger.Warnf("User %s login failed: %s", loginResp.Loginname, loginResp.Message)
	}
}

// forwardToClient 转发消息给客户端
func (gsh *GameServerHandler) forwardToClient(msgID uint32, sessionID string, data []byte) {
	// 查找客户端并转发
	msgData := util.Pack(msgID, sessionID, data)
	if client, exists := gsh.clientManager.GetClient(sessionID); exists {
		if err := client.Conn.WriteMessage(websocket.BinaryMessage, msgData); err != nil {
			util.Logger.Errorf("Failed to forward message to client sessionID:%s: %v", sessionID, err)
		}
	} else {
		util.Logger.Warnf("Client not found for sessionID:%s", sessionID)
	}
}

// handleKickPlayerNotify 处理踢出玩家通知
func (gsh *GameServerHandler) handleKickPlayerNotify(kickNotify *proto.KickPlayerNotify) {
	sessionID := kickNotify.SessionId
	reason := kickNotify.Reason

	util.Logger.Infof("Received KickPlayerNotify for sessionID: %s, reason: %v", sessionID, reason)

	// 查找对应的客户端
	client, exists := gsh.clientManager.GetClient(sessionID)
	if !exists {
		util.Logger.Warnf("Client not found for sessionID %s in KickPlayerNotify", sessionID)
		return
	}

	// 先转发消息给客户端
	data, err := protobuf.Marshal(kickNotify)
	if err != nil {
		util.Logger.Errorf("Failed to marshal KickPlayerNotify for sessionID %s: %v", sessionID, err)
	} else {
		msgData := util.Pack(10001, sessionID, data)
		if err := client.Conn.WriteMessage(websocket.BinaryMessage, msgData); err != nil {
			util.Logger.Errorf("Failed to send KickPlayerNotify to client %s: %v", sessionID, err)
		}
	}

	// 断开客户端连接并清理映射关系
	client.Conn.Close()
	gsh.clientManager.RemoveClient(sessionID)

	util.Logger.Infof("Client %s has been kicked and disconnected, reason: %v", sessionID, reason)
}
