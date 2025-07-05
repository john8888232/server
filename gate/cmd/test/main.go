package main

import (
	"encoding/binary"
	"fmt"
	"log"
	"time"

	"gate/proto" // 假设 proto 包路径

	"github.com/gorilla/websocket"
	protobuf "google.golang.org/protobuf/proto"
)

const (
	serverAddr = "ws://127.0.0.1:5600/ws" // 使用IPv4地址而不是localhost
)

// Pack 按照包头格式打包消息
func Pack(msgID uint32, sessionID string, data []byte) []byte {
	// 确保 sessionID 是 32 字节，如果不足则补零，如果过长则截断
	sessionIDBytes := make([]byte, 32)
	copy(sessionIDBytes, []byte(sessionID))

	// 计算总长度：length(4) + msgid(4) + sessionid(32) + data
	totalLength := 4 + 4 + 32 + len(data)
	msgData := make([]byte, totalLength)

	// 写入 length（包括 length 字段本身）
	binary.BigEndian.PutUint32(msgData[0:4], uint32(totalLength))
	// 写入 msgid
	binary.BigEndian.PutUint32(msgData[4:8], msgID)
	// 写入 sessionid
	copy(msgData[8:40], sessionIDBytes)
	// 写入 data
	copy(msgData[40:], data)

	return msgData
}

// Unpack 按照包头格式解包消息
func Unpack(data []byte) (uint32, string, []byte, error) {
	if len(data) < 40 { // length(4) + msgid(4) + sessionid(32)
		return 0, "", nil, fmt.Errorf("data too short to unpack")
	}

	// 读取 length（包括 length 字段本身）
	length := binary.BigEndian.Uint32(data[0:4])
	if len(data) < int(length) {
		return 0, "", nil, fmt.Errorf("data length mismatch")
	}

	// 读取 msgid
	msgID := binary.BigEndian.Uint32(data[4:8])
	// 读取 sessionid
	sessionID := string(data[8:40])
	// 读取 data
	payload := data[40:length]

	return msgID, sessionID, payload, nil
}

func main() {
	log.Println("开始连接服务器...")

	// 连接到WebSocket服务器
	conn, _, err := websocket.DefaultDialer.Dial(serverAddr, nil)
	if err != nil {
		log.Fatalf("连接服务器失败: %v", err)
	}
	defer conn.Close()

	log.Println("成功连接到服务器")

	// 创建登录请求
	loginReq := &proto.LoginReq{
		Loginname: "Luser123123",
		Token:     "123123",
		GameType:  "mines_pro",
		Version:   1,
		ClientIp:  "", // 由 Gateway 填充
	}

	log.Printf("准备发送登录请求:")
	log.Printf("  LoginName: %s", loginReq.Loginname)
	log.Printf("  Token: %s", loginReq.Token)
	log.Printf("  GameType: %s", loginReq.GameType)
	log.Printf("  Version: %d", loginReq.Version)

	// 序列化登录请求
	data, err := protobuf.Marshal(loginReq)
	if err != nil {
		log.Fatalf("序列化登录请求失败: %v", err)
	}

	// 打包消息 (使用消息ID 0x11001)
	msgData := Pack(0x11001, "", data)

	log.Printf("发送登录请求 (MsgID: 0x11001, 数据长度: %d字节)", len(data))

	// 发送登录请求
	err = conn.WriteMessage(websocket.BinaryMessage, msgData)
	if err != nil {
		log.Fatalf("发送登录请求失败: %v", err)
	}

	log.Println("登录请求已发送，等待服务器响应...")

	// 设置读取超时
	conn.SetReadDeadline(time.Now().Add(10 * time.Second))

	// 读取登录响应
	_, respData, err := conn.ReadMessage()
	if err != nil {
		log.Fatalf("读取登录响应失败: %v", err)
	}

	log.Printf("收到服务器响应 (数据长度: %d字节)", len(respData))

	// 解包响应消息
	msgID, sessionID, payload, err := Unpack(respData)
	if err != nil {
		log.Fatalf("解包登录响应失败: %v", err)
	}

	log.Printf("解包成功:")
	log.Printf("  消息ID: 0x%X", msgID)
	log.Printf("  会话ID: %s", sessionID)
	log.Printf("  载荷长度: %d字节", len(payload))

	// 检查消息ID是否为登录响应
	if msgID != 0x11002 {
		log.Fatalf("收到意外的消息ID: 0x%X, 期望: 0x11002", msgID)
	}

	// 反序列化登录响应
	var loginResp proto.LoginResp
	if err := protobuf.Unmarshal(payload, &loginResp); err != nil {
		log.Fatalf("反序列化登录响应失败: %v", err)
	}

	// 打印详细的登录响应
	log.Println("==================== 登录响应详情 ====================")
	log.Printf("登录名: %s", loginResp.Loginname)
	log.Printf("响应码: %d", loginResp.Code)
	log.Printf("响应消息: %s", loginResp.Message)

	if loginResp.Code == 0 {
		log.Println("登录成功! ✓")
		if loginResp.Info != nil {
			log.Println("玩家信息:")
			log.Printf("  用户名: %s", loginResp.Info.Username)
			log.Printf("  昵称: %s", loginResp.Info.Nickname)
			log.Printf("  头像: %s", loginResp.Info.Avatar)
			log.Printf("  VIP等级: %d", loginResp.Info.Vip)
			log.Printf("  余额: %.2f", loginResp.Info.Balance)
			log.Printf("  币种: %s", loginResp.Info.Currency)
		}
		log.Printf("会话ID: %s", sessionID)
		
		// 登录成功后启动心跳包发送
		log.Println("开始发送心跳包...")
		startHeartbeat(conn, sessionID)
		
		// 启动消息接收协程，处理服务器发来的消息（包括心跳响应）
		go func() {
			for {
				// 清除读取超时
				conn.SetReadDeadline(time.Time{})
				
				_, msgData, err := conn.ReadMessage()
				if err != nil {
					log.Printf("读取消息失败: %v", err)
					return
				}
				
				// 解包消息
				msgID, sessionID, payload, err := Unpack(msgData)
				if err != nil {
					log.Printf("解包消息失败: %v", err)
					continue
				}
				
				// 处理不同类型的消息
				switch msgID {
				case 0x11000: // 心跳响应
					var heartbeat proto.HeartBeat
					if err := protobuf.Unmarshal(payload, &heartbeat); err != nil {
						log.Printf("反序列化心跳响应失败: %v", err)
						continue
					}
					log.Printf("收到心跳响应 (时间戳: %d)", heartbeat.Timestamp)
				case 0x11006: // 踢出玩家通知
					var kickNotify proto.KickPlayerNotify
					if err := protobuf.Unmarshal(payload, &kickNotify); err != nil {
						log.Printf("反序列化踢出通知失败: %v", err)
						continue
					}
					log.Printf("收到踢出通知: 会话ID=%s, 原因=%v", kickNotify.SessionId, kickNotify.Reason)
					return
				default:
					log.Printf("收到未知消息: MsgID=0x%X, SessionID=%s, PayloadLen=%d", msgID, sessionID, len(payload))
				}
			}
		}()
		
		// 保持连接，让程序持续运行
		log.Println("客户端已启动，按 Ctrl+C 退出...")
		select {} // 无限阻塞，保持程序运行
	} else {
		log.Printf("登录失败! ✗ (错误码: %d)", loginResp.Code)
	}
	log.Println("=====================================================")
}

// sendHeartbeat 发送心跳包
func sendHeartbeat(conn *websocket.Conn, sessionID string) error {
	// 创建心跳包
	heartbeat := &proto.HeartBeat{
		Timestamp: time.Now().UnixMilli(), // 使用毫秒时间戳
	}

	// 序列化心跳包
	data, err := protobuf.Marshal(heartbeat)
	if err != nil {
		return fmt.Errorf("序列化心跳包失败: %v", err)
	}

	// 打包消息 (使用消息ID 0x11000)
	msgData := Pack(0x11000, sessionID, data)

	// 发送心跳包
	err = conn.WriteMessage(websocket.BinaryMessage, msgData)
	if err != nil {
		return fmt.Errorf("发送心跳包失败: %v", err)
	}

	log.Printf("发送心跳包成功 (MsgID: 0x11000, 时间戳: %d)", heartbeat.Timestamp)
	return nil
}

// startHeartbeat 启动心跳包发送协程
func startHeartbeat(conn *websocket.Conn, sessionID string) {
	ticker := time.NewTicker(5 * time.Second)
	go func() {
		defer ticker.Stop()
		for range ticker.C {
			if err := sendHeartbeat(conn, sessionID); err != nil {
				log.Printf("心跳包发送失败: %v", err)
				return
			}
		}
	}()
}
