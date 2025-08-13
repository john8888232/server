package main

import (
	"encoding/binary"
	"fmt"
	"log"
	"math/rand"
	"os"
	"sync"
	"time"

	"gate/proto"

	"github.com/gorilla/websocket"
	protobuf "google.golang.org/protobuf/proto"
)

const (
	serverAddr = "ws://127.0.0.1:5600/ws"
	maxRetries = 3
	retryDelay = 100 * time.Millisecond
)

// 添加一个全局互斥锁
var connMutex sync.Mutex

// 添加记录前后金额和时间的结构体
type BetDetail struct {
	BeforeAmount float64
	Amount       float64
	AfterAmount  float64
	Time         time.Time
}

// 添加记录兑现前后金额和时间的结构体
type CashDetail struct {
	PlayType     int32
	BeforeAmount float64
	Amount       float64
	AfterAmount  float64
	Multi        float64
	Time         time.Time
}

// 添加记录取消下注前后金额和时间的结构体
type CancelBetDetail struct {
	PlayType     int32
	RefundAmount float64
	BeforeAmount float64
	AfterAmount  float64
	Time         time.Time
}

// BotMode 机器人模式
type BotMode int

const (
	ModeBetAndCash BotMode = iota // 下注和兑现模式
	ModeCancelBet                 // 取消下注模式
)

// Player 玩家对象
type Player struct {
	Username         string
	Loginname        string
	Nickname         string
	Avatar           string
	Balance          float64
	Currency         string
	SessionID        string
	RoundID          string
	GameStatus       int32                     // 0-init 1-start jetton 2-stop jetton 3-settled
	BetPlayType      int32                     // 记录下注的玩法类型，0表示未下注
	Mode             BotMode                   // 机器人运行模式
	BetDetails       map[int32][]BetDetail     // 记录每种玩法的下注详情
	CashDetails      map[int32]CashDetail      // 记录每种玩法的兑现详情
	CancelBetDetails map[int32]CancelBetDetail // 记录每种玩法的取消下注详情
}

// Pack 按照包头格式打包消息
func Pack(msgID uint32, sessionID string, data []byte) []byte {
	sessionIDBytes := make([]byte, 32)
	copy(sessionIDBytes, []byte(sessionID))

	totalLength := 4 + 4 + 32 + len(data)
	msgData := make([]byte, totalLength)

	binary.BigEndian.PutUint32(msgData[0:4], uint32(totalLength))
	binary.BigEndian.PutUint32(msgData[4:8], msgID)
	copy(msgData[8:40], sessionIDBytes)
	copy(msgData[40:], data)

	return msgData
}

// Unpack 按照包头格式解包消息
func Unpack(data []byte) (uint32, string, []byte, error) {
	if len(data) < 40 {
		return 0, "", nil, fmt.Errorf("data too short to unpack")
	}

	length := binary.BigEndian.Uint32(data[0:4])
	if len(data) < int(length) {
		return 0, "", nil, fmt.Errorf("data length mismatch")
	}

	msgID := binary.BigEndian.Uint32(data[4:8])
	sessionID := string(data[8:40])
	payload := data[40:length]

	return msgID, sessionID, payload, nil
}

// showUsage 显示使用说明
func showUsage() {
	fmt.Println("扫雷游戏机器人")
	fmt.Println("")
	fmt.Println("用法:")
	fmt.Println("  mines_bot a    # 下注和兑现模式 (登录->下注->兑现)")
	fmt.Println("  mines_bot b    # 取消下注模式 (登录->下注->取消下注)")
	fmt.Println("")
	fmt.Println("说明:")
	fmt.Println("  模式a: 测试正常的下注和结算流程")
	fmt.Println("  模式b: 测试下注后立即取消下注的流程")
}

func main() {
	// 检查命令行参数
	if len(os.Args) != 2 {
		showUsage()
		os.Exit(1)
	}

	var mode BotMode
	switch os.Args[1] {
	case "a":
		mode = ModeBetAndCash
		log.Println("=== 扫雷游戏机器人启动 (下注和兑现模式) ===")
	case "b":
		mode = ModeCancelBet
		log.Println("=== 扫雷游戏机器人启动 (取消下注模式) ===")
	default:
		log.Printf("错误: 未知的模式参数 '%s'", os.Args[1])
		showUsage()
		os.Exit(1)
	}

	// 连接到WebSocket服务器
	conn, _, err := websocket.DefaultDialer.Dial(serverAddr, nil)
	if err != nil {
		log.Fatalf("连接服务器失败: %v", err)
	}
	defer conn.Close()

	log.Println("成功连接到服务器")

	// 创建玩家对象
	player := &Player{
		Mode:             mode,
		BetDetails:       make(map[int32][]BetDetail),
		CashDetails:      make(map[int32]CashDetail),
		CancelBetDetails: make(map[int32]CancelBetDetail),
	}

	// 发送登录请求
	if err := loginToGame(conn, player); err != nil {
		log.Fatalf("登录失败: %v", err)
	}

	// 启动心跳包发送
	startHeartbeat(conn, player.SessionID)

	// 启动消息处理循环
	handleMessages(conn, player)
}

// loginToGame 登录到游戏
func loginToGame(conn *websocket.Conn, player *Player) error {
	loginReq := &proto.LoginReq{
		Loginname: "Luser123123",
		Token:     "123123",
		GameType:  "mines_pro",
		Version:   1,
		ClientIp:  "",
	}

	log.Printf("发送登录请求:")
	log.Printf("  用户名: %s", loginReq.Loginname)
	log.Printf("  Token: %s", loginReq.Token)
	log.Printf("  游戏类型: %s", loginReq.GameType)

	data, err := protobuf.Marshal(loginReq)
	if err != nil {
		return fmt.Errorf("序列化登录请求失败: %v", err)
	}

	msgData := Pack(0x11001, "", data)
	log.Printf("📤 发送消息: MsgID=0x11001 (登录请求), 数据长度=%d", len(data))
	// 加锁保护WebSocket写入
	connMutex.Lock()
	err = conn.WriteMessage(websocket.BinaryMessage, msgData)
	connMutex.Unlock()
	if err != nil {
		return fmt.Errorf("发送登录请求失败: %v", err)
	}

	// 等待登录响应
	conn.SetReadDeadline(time.Now().Add(10 * time.Second))
	_, respData, err := conn.ReadMessage()
	if err != nil {
		return fmt.Errorf("读取登录响应失败: %v", err)
	}

	msgID, sessionID, payload, err := Unpack(respData)
	if err != nil {
		return fmt.Errorf("解包登录响应失败: %v", err)
	}

	log.Printf("📥 收到消息: MsgID=0x%X, SessionID=%s, 数据长度=%d", msgID, sessionID, len(payload))

	if msgID != 0x11002 {
		return fmt.Errorf("收到意外的消息ID: 0x%X, 期望: 0x11002", msgID)
	}

	var loginResp proto.LoginResp
	if err := protobuf.Unmarshal(payload, &loginResp); err != nil {
		return fmt.Errorf("反序列化登录响应失败: %v", err)
	}

	log.Println("==================== 登录响应 ====================")
	log.Printf("登录名: %s", loginResp.Loginname)
	log.Printf("响应码: %d", loginResp.Code)
	log.Printf("响应消息: %s", loginResp.Message)

	if loginResp.Code != 0 {
		// 失败时打印时间、errorcode、loginname
		log.Printf("FAILED LOGIN - Time: %s, ErrorCode: %d, LoginName: %s", 
			time.Now().Format("2006-01-02 15:04:05"), loginResp.Code, loginResp.Loginname)
		return fmt.Errorf("登录失败: %s (错误码: %d)", loginResp.Message, loginResp.Code)
	}

	// 初始化玩家对象
	if loginResp.Info != nil {
		player.Username = loginResp.Info.Username
		player.Loginname = loginResp.Info.Loginname
		player.Nickname = loginResp.Info.Nickname
		player.Avatar = loginResp.Info.Avatar
		player.Balance = loginResp.Info.Balance
		player.Currency = loginResp.Info.Currency
		player.SessionID = sessionID

		log.Println("玩家信息初始化完成:")
		log.Printf("  用户名: %s", player.Username)
		log.Printf("  登录名: %s", player.Loginname)
		log.Printf("  昵称: %s", player.Nickname)
		log.Printf("  头像: %s", player.Avatar)
		log.Printf("  余额: %.2f %s", player.Balance, player.Currency)
		log.Printf("  会话ID: %s", player.SessionID)
	}
	log.Println("================================================")

	return nil
}

// startHeartbeat 启动心跳包发送
func startHeartbeat(conn *websocket.Conn, sessionID string) {
	ticker := time.NewTicker(5 * time.Second)
	log.Printf("启动心跳包发送，间隔5秒")
	go func() {
		defer ticker.Stop()
		for range ticker.C {
			// 将时间戳转换为字符串
			timestampStr := fmt.Sprintf("%d", time.Now().UnixMilli())
			heartbeat := &proto.HeartBeat{
				Timestamp: timestampStr,
			}

			data, err := protobuf.Marshal(heartbeat)
			if err != nil {
				log.Printf("序列化心跳包失败: %v", err)
				continue
			}

			msgData := Pack(0x11000, sessionID, data)
			log.Printf("发送心跳包: SessionID=%s, 时间戳=%s", sessionID, timestampStr)
			// 加锁保护WebSocket写入
			connMutex.Lock()
			err = conn.WriteMessage(websocket.BinaryMessage, msgData)
			connMutex.Unlock()
			if err != nil {
				log.Printf("发送心跳包失败: %v", err)
				return
			}
		}
	}()
}

// handleMessages 处理消息
func handleMessages(conn *websocket.Conn, player *Player) {
	for {
		conn.SetReadDeadline(time.Time{})
		_, msgData, err := conn.ReadMessage()
		if err != nil {
			log.Printf("读取消息失败: %v", err)
			return
		}

		msgID, sessionID, payload, err := Unpack(msgData)
		if err != nil {
			log.Printf("解包消息失败: %v", err)
			continue
		}

		// 打印所有收到的消息（除了心跳响应）
		if msgID != 0x11000 {
			log.Printf("📥 收到消息: MsgID=0x%X, SessionID=%s, 数据长度=%d", msgID, sessionID, len(payload))
		}

		// 处理不同类型的消息
		switch msgID {
		case 0x11000: // 心跳响应
			log.Printf("收到心跳响应: SessionID=%s, 数据长度=%d", sessionID, len(payload))
			// 打印心跳响应的包体
			log.Printf("📦 心跳响应包体: [% X]", payload)

			// 解析心跳响应
			var heartbeatResp proto.HeartBeat
			if err := protobuf.Unmarshal(payload, &heartbeatResp); err != nil {
				log.Printf("解析心跳响应失败: %v", err)
			} else {
				log.Printf("心跳响应时间戳: %s", heartbeatResp.Timestamp)
			}
			continue

		case 0x11006: // 踢出玩家通知
			handleKickPlayerNotify(payload)
			return

		case 0x11008: // 游戏快照通知
			handleGameSnapshot(conn, payload, player)

		case 0x2000A: // 开始下注通知
			handleStartJettonNotify(conn, payload, player)

		case 0x2000C: // 停止下注通知
			handleStopJettonNotify(conn, payload, player)

		case 0x20002: // 下注响应
			handlePlaceBetResponse(conn, payload, player)

		case 0x20006: // 兑现响应
			handleCashResponse(payload, player)

		case 0x20008: // 取消下注响应
			handleCancelBetResponse(payload, player)

		case 0x2000E: // 榜单信息通知
			handleRankInfoNotify(payload)

		default:
			log.Printf("收到未知消息: MsgID=0x%X, SessionID=%s, PayloadLen=%d", msgID, sessionID, len(payload))
		}
	}
}

// handleKickPlayerNotify 处理踢出玩家通知
func handleKickPlayerNotify(payload []byte) {
	var kickNotify proto.KickPlayerNotify
	if err := protobuf.Unmarshal(payload, &kickNotify); err != nil {
		log.Printf("反序列化踢出通知失败: %v", err)
		return
	}
	log.Printf("==================== 踢出通知 ====================")
	log.Printf("会话ID: %s", kickNotify.SessionId)
	log.Printf("踢出原因: %v", kickNotify.Reason)
	log.Println("================================================")
}

// triggerBetting 触发下注逻辑的共用函数
func triggerBetting(conn *websocket.Conn, player *Player, reason string) {
	// 固定使用玩法类型1进行下注
	playType := int32(1)
	amount := 10.0 + rand.Float64()*90.0 // 10-100之间的随机金额

	log.Printf("触发下注 (%s): 类型=%d, 金额=%.2f", reason, playType, amount)

	// 使用重试机制发送下注请求
	go func() {
		// 延迟一小段时间再下注，避免与状态转换冲突
		time.Sleep(200 * time.Millisecond)

		success := sendPlaceBetWithRetry(conn, player, playType, amount)
		if success {
			// 记录下注的玩法类型
			player.BetPlayType = playType
			log.Printf("记录下注玩法类型: %d", playType)
		} else {
			log.Printf("下注请求最终失败: 类型=%d, 金额=%.2f", playType, amount)
		}
	}()
}

// triggerCancelBet 触发取消下注逻辑
func triggerCancelBet(conn *websocket.Conn, player *Player, reason string) {
	if player.BetPlayType == 0 {
		log.Printf("未下注，无需取消")
		return
	}

	log.Printf("触发取消下注 (%s): 类型=%d", reason, player.BetPlayType)

	// 延迟一段时间后取消下注，确保下注已经完成
	go func() {
		time.Sleep(1 * time.Second)

		success := sendCancelBetWithRetry(conn, player, player.BetPlayType)
		if !success {
			log.Printf("取消下注请求最终失败: 类型=%d", player.BetPlayType)
		}
	}()
}

// handleGameSnapshot 处理游戏快照通知
func handleGameSnapshot(conn *websocket.Conn, payload []byte, player *Player) {
	var snapshot proto.GameSnapshotNotify
	if err := protobuf.Unmarshal(payload, &snapshot); err != nil {
		log.Printf("反序列化游戏快照失败: %v", err)
		return
	}

	// 更新玩家状态
	player.RoundID = snapshot.RoundId
	player.GameStatus = snapshot.Status

	log.Printf("==================== 游戏快照 ====================")
	log.Printf("回合ID: %s", snapshot.RoundId)
	log.Printf("游戏类型: %s", snapshot.GameType)
	log.Printf("游戏状态: %d (0-init 1-start jetton 2-stop jetton 3-settled)", snapshot.Status)
	log.Printf("剩余时间: %d秒", snapshot.RemainTime)
	log.Printf("当前索引: %d", snapshot.CurIndex)
	log.Printf("当前倍数: %.2f", snapshot.CurMulti)
	log.Printf("游戏结果数量: %d", len(snapshot.Result))

	// 根据游戏状态显示hash和seed
	if snapshot.Status == 1 || snapshot.Status == 2 || snapshot.Status == 3 {
		log.Printf("结果哈希: %s", snapshot.Hash)
	}

	if snapshot.Status == 3 {
		log.Printf("结果种子: %s", snapshot.Seed)
	}

	log.Println("================================================")

	// 如果游戏状态是START_JETTON且剩余时间充足，触发下注
	if snapshot.Status == 1 && snapshot.RemainTime > 0 && player.BetPlayType == 0 {
		log.Printf("检测到游戏处于下注阶段，剩余时间: %d秒，触发下注", snapshot.RemainTime)
		triggerBetting(conn, player, "游戏快照检测")
	} else {
		log.Printf("下注条件检查: Status=%d, RemainTime=%d, BetPlayType=%d",
			snapshot.Status, snapshot.RemainTime, player.BetPlayType)
	}
}

// handleStartJettonNotify 处理开始下注通知
func handleStartJettonNotify(conn *websocket.Conn, payload []byte, player *Player) {
	var startNotify proto.MinesStartJettonNotify
	if err := protobuf.Unmarshal(payload, &startNotify); err != nil {
		log.Printf("反序列化开始下注通知失败: %v", err)
		return
	}

	log.Printf("==================== 开始下注 ====================")
	log.Printf("回合ID: %s", startNotify.RoundId)
	log.Printf("游戏类型: %s", startNotify.Gametype)
	log.Printf("下注持续时间: %d秒", startNotify.Duration)
	log.Printf("结果哈希: %s", startNotify.Hash)
	log.Printf("机器人模式: %v", player.Mode)
	log.Println("================================================")

	// 更新回合ID
	player.RoundID = startNotify.RoundId
	// 重置下注状态
	player.BetPlayType = 0

	triggerBetting(conn, player, "开始下注通知")
}

// sendPlaceBetWithRetry 带重试机制的下注请求
func sendPlaceBetWithRetry(conn *websocket.Conn, player *Player, playType int32, amount float64) bool {
	// 不再需要在这里记录，在响应处理中记录即可
	placeBetReq := &proto.MinesPlaceBetReq{
		Loginname: player.Loginname,
		RoundId:   player.RoundID,
		PlayType:  playType,
		Amount:    amount,
	}

	for attempt := 1; attempt <= maxRetries; attempt++ {
		log.Printf("发送下注请求 (尝试 %d/%d): 类型=%d, 金额=%.2f", attempt, maxRetries, playType, amount)

		data, err := protobuf.Marshal(placeBetReq)
		if err != nil {
			log.Printf("序列化下注请求失败: %v", err)
			return false
		}

		msgData := Pack(0x20001, player.SessionID, data)
		log.Printf("📤 发送消息: MsgID=0x20001 (下注请求), 数据长度=%d", len(data))
		// 加锁保护WebSocket写入
		connMutex.Lock()
		err = conn.WriteMessage(websocket.BinaryMessage, msgData)
		connMutex.Unlock()
		if err != nil {
			log.Printf("发送下注请求失败 (尝试 %d/%d): %v", attempt, maxRetries, err)
			if attempt < maxRetries {
				time.Sleep(retryDelay * time.Duration(attempt))
				continue
			}
			return false
		}

		log.Printf("下注请求发送成功 (尝试 %d/%d)", attempt, maxRetries)
		return true
	}

	return false
}

// sendCancelBetWithRetry 带重试机制的取消下注请求
func sendCancelBetWithRetry(conn *websocket.Conn, player *Player, playType int32) bool {
	cancelBetReq := &proto.MinesCancelBetReq{
		Loginname: player.Loginname,
		RoundId:   player.RoundID,
		PlayType:  playType,
	}

	for attempt := 1; attempt <= maxRetries; attempt++ {
		log.Printf("发送取消下注请求 (尝试 %d/%d): 类型=%d", attempt, maxRetries, playType)

		data, err := protobuf.Marshal(cancelBetReq)
		if err != nil {
			log.Printf("序列化取消下注请求失败: %v", err)
			return false
		}

		msgData := Pack(0x20007, player.SessionID, data)
		log.Printf("📤 发送消息: MsgID=0x20007 (取消下注请求), 数据长度=%d", len(data))
		// 加锁保护WebSocket写入
		connMutex.Lock()
		err = conn.WriteMessage(websocket.BinaryMessage, msgData)
		connMutex.Unlock()
		if err != nil {
			log.Printf("发送取消下注请求失败 (尝试 %d/%d): %v", attempt, maxRetries, err)
			if attempt < maxRetries {
				time.Sleep(retryDelay * time.Duration(attempt))
				continue
			}
			return false
		}

		log.Printf("取消下注请求发送成功 (尝试 %d/%d)", attempt, maxRetries)
		return true
	}

	return false
}

// handleStopJettonNotify 处理停止下注通知
func handleStopJettonNotify(conn *websocket.Conn, payload []byte, player *Player) {
	var stopNotify proto.GameStopJettonNotify
	if err := protobuf.Unmarshal(payload, &stopNotify); err != nil {
		log.Printf("反序列化停止下注通知失败: %v", err)
		return
	}

	log.Printf("==================== 停止下注 ====================")
	log.Printf("回合ID: %s", stopNotify.RoundId)
	log.Printf("游戏类型: %s", stopNotify.GameType)
	log.Printf("机器人模式: %v", player.Mode)
	log.Println("================================================")

	// 根据模式决定操作
	switch player.Mode {
	case ModeBetAndCash:
		// 下注和兑现模式：检查是否已下注，进行兑现
		if player.BetPlayType == 0 {
			log.Printf("本轮未下注，跳过兑现")
			return
		}

		// 使用重试机制发送兑现请求，使用与下注相同的玩法类型
		go func() {
			// 延迟一小段时间再兑现，避免与游戏逻辑冲突
			time.Sleep(300 * time.Millisecond)

			success := sendCashWithRetry(conn, player, player.BetPlayType)
			if !success {
				log.Printf("兑现请求最终失败: 类型=%d", player.BetPlayType)
			}
		}()

	case ModeCancelBet:
		// 取消下注模式：如果已下注，则取消下注
		if player.BetPlayType != 0 {
			triggerCancelBet(conn, player, "停止下注阶段")
		}
	}
}

// sendCashWithRetry 带重试机制的兑现请求
func sendCashWithRetry(conn *websocket.Conn, player *Player, playType int32) bool {
	cashReq := &proto.MinesCashReq{
		Loginname: player.Loginname,
		RoundId:   player.RoundID,
		PlayType:  playType,
	}

	for attempt := 1; attempt <= maxRetries; attempt++ {
		log.Printf("发送兑现请求 (尝试 %d/%d): 类型=%d", attempt, maxRetries, playType)

		data, err := protobuf.Marshal(cashReq)
		if err != nil {
			log.Printf("序列化兑现请求失败: %v", err)
			return false
		}

		msgData := Pack(0x20005, player.SessionID, data)
		log.Printf("📤 发送消息: MsgID=0x20005 (兑现请求), 数据长度=%d", len(data))
		// 加锁保护WebSocket写入
		connMutex.Lock()
		err = conn.WriteMessage(websocket.BinaryMessage, msgData)
		connMutex.Unlock()
		if err != nil {
			log.Printf("发送兑现请求失败 (尝试 %d/%d): %v", attempt, maxRetries, err)
			if attempt < maxRetries {
				time.Sleep(retryDelay * time.Duration(attempt))
				continue
			}
			return false
		}

		log.Printf("兑现请求发送成功 (尝试 %d/%d)", attempt, maxRetries)
		return true
	}

	return false
}

// handlePlaceBetResponse 处理下注响应
func handlePlaceBetResponse(conn *websocket.Conn, payload []byte, player *Player) {
	var betResp proto.MinesPlaceBetRes
	if err := protobuf.Unmarshal(payload, &betResp); err != nil {
		log.Printf("反序列化下注响应失败: %v", err)
		return
	}

	log.Printf("==================== 下注响应 ====================")
	log.Printf("响应码: %d", betResp.Code)
	log.Printf("响应消息: %s", betResp.Message)
	log.Printf("回合ID: %s", betResp.RoundId)
	if betResp.Bet != nil {
		log.Printf("下注记录: 类型=%d, 金额=%.2f", betResp.Bet.PlayType, betResp.Bet.Amount)
	}
	log.Printf("下注后余额: %.2f", betResp.Balance)

	// 检查是否是服务器忙碌或死锁相关错误
	if betResp.Code != 0 {
		// 失败时打印时间、errorcode、loginname
		log.Printf("❌ FAILED BET - Time: %s, ErrorCode: %d, LoginName: %s", 
			time.Now().Format("2006-01-02 15:04:05"), betResp.Code, player.Loginname)
		log.Printf("⚠️  下注失败 - 错误码: %d, 消息: %s", betResp.Code, betResp.Message)
		if betResp.Message == "Game is busy, please try again" ||
			betResp.Message == "Game is transitioning, please wait" {
			log.Printf("🔄 检测到服务器忙碌/状态转换，这是正常的重试场景")
		}
	} else {
		log.Printf("✅ 下注成功")

		// 记录下注详情
		if betResp.Bet != nil {
			playType := betResp.Bet.PlayType
			amount := betResp.Bet.Amount
			beforeAmount := player.Balance + amount // 估算下注前余额
			afterAmount := betResp.Balance

			// 创建下注详情记录
			betDetail := BetDetail{
				BeforeAmount: beforeAmount,
				Amount:       amount,
				AfterAmount:  afterAmount,
				Time:         time.Now(),
			}

			// 添加到玩家的下注详情记录中
			player.BetDetails[playType] = append(player.BetDetails[playType], betDetail)

			log.Printf("记录下注详情: 类型=%d, 前余额=%.2f, 金额=%.2f, 后余额=%.2f",
				playType, beforeAmount, amount, afterAmount)
		}

		// 更新玩家余额
		player.Balance = betResp.Balance

		// 如果是取消下注模式，在下注成功后触发取消下注
		if player.Mode == ModeCancelBet {
			triggerCancelBet(conn, player, "下注成功后")
		}
	}
	log.Println("================================================")
}

// handleCancelBetResponse 处理取消下注响应
func handleCancelBetResponse(payload []byte, player *Player) {
	var cancelResp proto.MinesCancelBetRes
	if err := protobuf.Unmarshal(payload, &cancelResp); err != nil {
		log.Printf("反序列化取消下注响应失败: %v", err)
		return
	}

	log.Printf("==================== 取消下注响应 ====================")
	log.Printf("回合ID: %s", cancelResp.RoundId)
	log.Printf("响应码: %d", cancelResp.Code)
	log.Printf("响应消息: %s", cancelResp.Message)
	log.Printf("退还金额: %.2f", cancelResp.RefundAmount)
	log.Printf("取消后余额: %.2f", cancelResp.Balance)
	log.Printf("下注类型: %d", cancelResp.PlayType)

	// 检查取消下注结果
	if cancelResp.Code != 0 {
		// 失败时打印时间、errorcode、loginname
		log.Printf("❌ FAILED CANCEL_BET - Time: %s, ErrorCode: %d, LoginName: %s", 
			time.Now().Format("2006-01-02 15:04:05"), cancelResp.Code, player.Loginname)
		log.Printf("⚠️  取消下注失败 - 错误码: %d, 消息: %s", cancelResp.Code, cancelResp.Message)
	} else {
		log.Printf("✅ 取消下注成功，退还金额: %.2f", cancelResp.RefundAmount)

		// 记录取消下注详情
		playType := cancelResp.PlayType
		refundAmount := cancelResp.RefundAmount
		beforeAmount := player.Balance // 当前余额是取消前的余额
		afterAmount := cancelResp.Balance

		// 创建取消下注详情记录
		cancelBetDetail := CancelBetDetail{
			PlayType:     playType,
			RefundAmount: refundAmount,
			BeforeAmount: beforeAmount,
			AfterAmount:  afterAmount,
			Time:         time.Now(),
		}

		// 添加到玩家的取消下注详情记录中
		player.CancelBetDetails[playType] = cancelBetDetail

		log.Printf("记录取消下注详情: 类型=%d, 前余额=%.2f, 退款金额=%.2f, 后余额=%.2f",
			playType, beforeAmount, refundAmount, afterAmount)

		// 更新玩家余额
		player.Balance = cancelResp.Balance
		// 重置下注状态
		player.BetPlayType = 0
	}
	log.Println("================================================")
}

// handleCashResponse 处理兑现响应
func handleCashResponse(payload []byte, player *Player) {
	var cashResp proto.MinesCashRes
	if err := protobuf.Unmarshal(payload, &cashResp); err != nil {
		log.Printf("反序列化兑现响应失败: %v", err)
		return
	}

	log.Printf("==================== 兑现响应 ====================")
	log.Printf("回合ID: %s", cashResp.RoundId)
	log.Printf("响应码: %d", cashResp.Code)
	log.Printf("响应消息: %s", cashResp.Message)
	if cashResp.Reckon != nil {
		log.Printf("派奖记录: 类型=%d, 金额=%.2f, 倍数=%.2f", cashResp.Reckon.PlayType, cashResp.Reckon.Amount, cashResp.Reckon.Multi)
	}
	log.Printf("兑现后余额: %.2f", cashResp.Balance)
	log.Printf("兑现类型: %d", cashResp.PlayType)

	// 检查兑现结果
	if cashResp.Code != 0 {
		// 失败时打印时间、errorcode、loginname
		log.Printf("❌ FAILED CASH - Time: %s, ErrorCode: %d, LoginName: %s", 
			time.Now().Format("2006-01-02 15:04:05"), cashResp.Code, player.Loginname)
		log.Printf("⚠️  兑现失败 - 错误码: %d, 消息: %s", cashResp.Code, cashResp.Message)
	} else {
		log.Printf("✅ 兑现成功")
		// 记录兑现详情
		if cashResp.Reckon != nil {
			playType := cashResp.Reckon.PlayType
			amount := cashResp.Reckon.Amount
			beforeAmount := player.Balance - amount // 估算兑现前余额
			afterAmount := cashResp.Balance
			multi := cashResp.Reckon.Multi

			// 创建兑现详情记录
			cashDetail := CashDetail{
				PlayType:     playType,
				BeforeAmount: beforeAmount,
				Amount:       amount,
				AfterAmount:  afterAmount,
				Multi:        multi,
				Time:         time.Now(),
			}

			// 添加到玩家的下注详情记录中
			player.CashDetails[playType] = cashDetail

			log.Printf("记录兑现详情: 类型=%d, 前余额=%.2f, 金额=%.2f, 后余额=%.2f, 倍数=%.2f",
				playType, beforeAmount, amount, afterAmount, multi)
		}
		// 更新玩家余额
		player.Balance = cashResp.Balance
	}
	log.Println("================================================")
}

// handleRankInfoNotify 处理榜单信息通知
func handleRankInfoNotify(payload []byte) {
	var rankInfo proto.GameRankInfoNotify
	if err := protobuf.Unmarshal(payload, &rankInfo); err != nil {
		log.Printf("反序列化榜单信息失败: %v", err)
		return
	}

	log.Printf("==================== 榜单信息 ====================")
	log.Printf("回合ID: %s", rankInfo.RoundId)
	log.Printf("游戏类型: %s", rankInfo.GameType)
	log.Printf("玩家数量: %d", len(rankInfo.Players))

	// 只显示前3个玩家的详细信息，避免日志过多
	maxDetailPlayers := 3
	if len(rankInfo.Players) > 0 {
		displayCount := min(len(rankInfo.Players), maxDetailPlayers)
		log.Printf("显示前%d名玩家详细信息:", displayCount)

		for i := 0; i < displayCount; i++ {
			playerSnap := rankInfo.Players[i]
			if playerSnap.Info != nil {
				log.Printf("  玩家[%d]: %s (昵称: %s), 余额=%.2f",
					i, playerSnap.Info.Loginname, playerSnap.Info.Nickname, playerSnap.Info.Balance)

				// 显示下注记录
				if len(playerSnap.Bets) > 0 {
					log.Printf("    下注记录数: %d", len(playerSnap.Bets))
					for j, bet := range playerSnap.Bets {
						log.Printf("      下注[%d]: 类型=%d, 金额=%.2f",
							j, bet.PlayType, bet.Amount)
					}
				}

				// 显示派奖记录
				if len(playerSnap.Reckons) > 0 {
					log.Printf("    派奖记录数: %d", len(playerSnap.Reckons))
					for j, reckon := range playerSnap.Reckons {
						log.Printf("      派奖[%d]: 类型=%d, 金额=%.2f, 倍数=%.2f",
							j, reckon.PlayType, reckon.Amount, reckon.Multi)
					}
				}
			}
		}

		// 如果玩家数量超过显示的数量，只显示剩余玩家数量
		if len(rankInfo.Players) > displayCount {
			log.Printf("  ...以及其他%d名玩家", len(rankInfo.Players)-displayCount)
		}
	} else {
		log.Printf("榜单中没有玩家")
	}

	log.Println("================================================")
}

// min 返回两个整数中的较小值
func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
