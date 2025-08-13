package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"log"
	"math/rand"
	"os"
	"os/signal"
	"sync"
	"sync/atomic"
	"syscall"
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

// Statistics 统计信息
type Statistics struct {
	TotalBots        int64
	LoginSuccess     int64
	LoginFailed      int64
	BetSuccess       int64
	BetFailed        int64
	CashSuccess      int64
	CashFailed       int64
	CancelBetSuccess int64
	CancelBetFailed  int64
	TotalErrors      int64
}

// Bot 压测机器人
type Bot struct {
	ID        int
	Loginname string
	Token     string
	SessionID string
	Balance   float64
	RoundID   string
	BetAmount float64
	PlayType  int32
	IsActive  bool
	Stats     *Statistics
	mu        sync.Mutex
}

// Config 配置信息
type Config struct {
	BotCount       int
	ServerAddr     string
	BetAmountMin   float64
	BetAmountMax   float64
	CancelBetRatio float64 // 取消下注比例 (0.0-1.0)
}

var (
	globalStats = &Statistics{}
	bots        []*Bot
	config      Config
)

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

// NewBot 创建新的机器人
func NewBot(id int, stats *Statistics) *Bot {
	loginname := fmt.Sprintf("Ltest%06d", id)
	return &Bot{
		ID:        id,
		Loginname: loginname,
		Token:     "abcabc",
		Stats:     stats,
		IsActive:  true,
	}
}

// Login 机器人登录
func (b *Bot) Login() (*websocket.Conn, error) {
	conn, _, err := websocket.DefaultDialer.Dial(config.ServerAddr, nil)
	if err != nil {
		atomic.AddInt64(&b.Stats.LoginFailed, 1)
		return nil, fmt.Errorf("连接失败: %v", err)
	}

	loginReq := &proto.LoginReq{
		Loginname: b.Loginname,
		Token:     b.Token,
		GameType:  "mines_pro",
		Version:   1,
		ClientIp:  "",
	}

	data, err := protobuf.Marshal(loginReq)
	if err != nil {
		conn.Close()
		atomic.AddInt64(&b.Stats.LoginFailed, 1)
		return nil, fmt.Errorf("序列化失败: %v", err)
	}

	msgData := Pack(0x11001, "", data)
	
	// 记录发送时间
	sendTime := time.Now()
	log.Printf("Bot %d 发送登录请求时间: %s", b.ID, sendTime.Format("2006-01-02 15:04:05.000"))
	
	if err = conn.WriteMessage(websocket.BinaryMessage, msgData); err != nil {
		conn.Close()
		atomic.AddInt64(&b.Stats.LoginFailed, 1)
		return nil, fmt.Errorf("发送失败: %v", err)
	}

	// 等待登录响应
	conn.SetReadDeadline(time.Now().Add(10 * time.Second))
	_, respData, err := conn.ReadMessage()
	
	// 记录接收时间或超时时间
	if err != nil {
		receiveTime := time.Now()
		duration := receiveTime.Sub(sendTime)
		log.Printf("Bot %d 登录失败: %v (发送时间: %s, 失败时间: %s, 耗时: %v)", 
			b.ID, err, sendTime.Format("2006-01-02 15:04:05.000"), 
			receiveTime.Format("2006-01-02 15:04:05.000"), duration)
		conn.Close()
		atomic.AddInt64(&b.Stats.LoginFailed, 1)
		return nil, fmt.Errorf("读取响应失败: %v", err)
	}
	
	// 记录成功接收时间
	receiveTime := time.Now()
	duration := receiveTime.Sub(sendTime)
	log.Printf("Bot %d 登录响应接收时间: %s (耗时: %v)", b.ID, receiveTime.Format("2006-01-02 15:04:05.000"), duration)

	msgID, sessionID, payload, err := Unpack(respData)
	if err != nil {
		conn.Close()
		atomic.AddInt64(&b.Stats.LoginFailed, 1)
		return nil, fmt.Errorf("解包失败: %v", err)
	}

	if msgID != 0x11002 {
		conn.Close()
		atomic.AddInt64(&b.Stats.LoginFailed, 1)
		return nil, fmt.Errorf("消息ID错误: 0x%X", msgID)
	}

	var loginResp proto.LoginResp
	if err := protobuf.Unmarshal(payload, &loginResp); err != nil {
		conn.Close()
		atomic.AddInt64(&b.Stats.LoginFailed, 1)
		return nil, fmt.Errorf("反序列化失败: %v", err)
	}

	if loginResp.Code != 0 {
		conn.Close()
		atomic.AddInt64(&b.Stats.LoginFailed, 1)
		return nil, fmt.Errorf("登录失败: %s", loginResp.Message)
	}

	b.mu.Lock()
	b.SessionID = sessionID
	if loginResp.Info != nil {
		b.Balance = loginResp.Info.Balance
	}
	b.mu.Unlock()

	atomic.AddInt64(&b.Stats.LoginSuccess, 1)
	return conn, nil
}

// PlaceBet 下注
func (b *Bot) PlaceBet(conn *websocket.Conn) error {
	b.mu.Lock()
	if b.RoundID == "" {
		b.mu.Unlock()
		return fmt.Errorf("没有有效的回合ID")
	}

	// 随机选择玩法1或2
	playType := int32(1)
	if rand.Float64() < 0.5 {
		playType = 2
	}

	// 随机下注金额
	amount := config.BetAmountMin + rand.Float64()*(config.BetAmountMax-config.BetAmountMin)
	
	b.PlayType = playType
	b.BetAmount = amount
	roundID := b.RoundID
	b.mu.Unlock()

	placeBetReq := &proto.MinesPlaceBetReq{
		Loginname: b.Loginname,
		RoundId:   roundID,
		PlayType:  playType,
		Amount:    amount,
	}

	data, err := protobuf.Marshal(placeBetReq)
	if err != nil {
		atomic.AddInt64(&b.Stats.BetFailed, 1)
		return fmt.Errorf("序列化下注请求失败: %v", err)
	}

	msgData := Pack(0x20001, b.SessionID, data)
	if err = conn.WriteMessage(websocket.BinaryMessage, msgData); err != nil {
		atomic.AddInt64(&b.Stats.BetFailed, 1)
		return fmt.Errorf("发送下注请求失败: %v", err)
	}

	return nil
}

// CancelBet 取消下注
func (b *Bot) CancelBet(conn *websocket.Conn) error {
	b.mu.Lock()
	if b.RoundID == "" || b.PlayType == 0 {
		b.mu.Unlock()
		return fmt.Errorf("没有可取消的下注")
	}

	playType := b.PlayType
	roundID := b.RoundID
	b.mu.Unlock()

	cancelBetReq := &proto.MinesCancelBetReq{
		Loginname: b.Loginname,
		RoundId:   roundID,
		PlayType:  playType,
	}

	data, err := protobuf.Marshal(cancelBetReq)
	if err != nil {
		atomic.AddInt64(&b.Stats.CancelBetFailed, 1)
		return fmt.Errorf("序列化取消下注请求失败: %v", err)
	}

	msgData := Pack(0x20007, b.SessionID, data)
	if err = conn.WriteMessage(websocket.BinaryMessage, msgData); err != nil {
		atomic.AddInt64(&b.Stats.CancelBetFailed, 1)
		return fmt.Errorf("发送取消下注请求失败: %v", err)
	}

	return nil
}

// Cash 提现
func (b *Bot) Cash(conn *websocket.Conn) error {
	b.mu.Lock()
	if b.RoundID == "" || b.PlayType == 0 {
		b.mu.Unlock()
		return fmt.Errorf("没有可提现的下注")
	}

	playType := b.PlayType
	roundID := b.RoundID
	b.mu.Unlock()

	cashReq := &proto.MinesCashReq{
		Loginname: b.Loginname,
		RoundId:   roundID,
		PlayType:  playType,
	}

	data, err := protobuf.Marshal(cashReq)
	if err != nil {
		atomic.AddInt64(&b.Stats.CashFailed, 1)
		return fmt.Errorf("序列化提现请求失败: %v", err)
	}

	msgData := Pack(0x20005, b.SessionID, data)
	if err = conn.WriteMessage(websocket.BinaryMessage, msgData); err != nil {
		atomic.AddInt64(&b.Stats.CashFailed, 1)
		return fmt.Errorf("发送提现请求失败: %v", err)
	}

	return nil
}

// HandleMessage 处理消息
func (b *Bot) HandleMessage(conn *websocket.Conn, msgID uint32, payload []byte) {
	switch msgID {
	case 0x11008: // 游戏快照
		b.handleGameSnapshot(conn, payload)
	case 0x2000A: // 开始下注通知
		b.handleStartJetton(conn, payload)
	case 0x2000C: // 停止下注通知
		b.handleStopJetton(conn, payload)
	case 0x20002: // 下注响应
		b.handlePlaceBetResponse(payload)
	case 0x20006: // 提现响应
		b.handleCashResponse(payload)
	case 0x20008: // 取消下注响应
		b.handleCancelBetResponse(payload)
	}
}

func (b *Bot) handleGameSnapshot(conn *websocket.Conn, payload []byte) {
	var snapshot proto.GameSnapshotNotify
	if err := protobuf.Unmarshal(payload, &snapshot); err != nil {
		return
	}

	b.mu.Lock()
	b.RoundID = snapshot.RoundId
	b.mu.Unlock()

	// 如果游戏在下注阶段且剩余时间充足，尝试下注
	if snapshot.Status == 1 && snapshot.RemainTime > 2 {
		go func() {
			time.Sleep(time.Duration(rand.Intn(2000)) * time.Millisecond)
			b.PlaceBet(conn)
		}()
	}
}

func (b *Bot) handleStartJetton(conn *websocket.Conn, payload []byte) {
	var startNotify proto.MinesStartJettonNotify
	if err := protobuf.Unmarshal(payload, &startNotify); err != nil {
		return
	}

	b.mu.Lock()
	b.RoundID = startNotify.RoundId
	b.PlayType = 0 // 重置下注状态
	b.mu.Unlock()

	// 随机延迟后下注
	go func() {
		delay := time.Duration(rand.Intn(3000)) * time.Millisecond
		time.Sleep(delay)
		b.PlaceBet(conn)
	}()
}

func (b *Bot) handleStopJetton(conn *websocket.Conn, payload []byte) {
	// 决定是提现还是取消下注
	if rand.Float64() < config.CancelBetRatio {
		// 取消下注
		go func() {
			time.Sleep(time.Duration(rand.Intn(1000)) * time.Millisecond)
			b.CancelBet(conn)
		}()
	} else {
		// 提现
		go func() {
			time.Sleep(time.Duration(rand.Intn(1000)) * time.Millisecond)
			b.Cash(conn)
		}()
	}
}

func (b *Bot) handlePlaceBetResponse(payload []byte) {
	var betResp proto.MinesPlaceBetRes
	if err := protobuf.Unmarshal(payload, &betResp); err != nil {
		return
	}

	if betResp.Code == 0 {
		atomic.AddInt64(&b.Stats.BetSuccess, 1)
		b.mu.Lock()
		b.Balance = betResp.Balance
		b.mu.Unlock()
	} else {
		atomic.AddInt64(&b.Stats.BetFailed, 1)
	}
}

func (b *Bot) handleCashResponse(payload []byte) {
	var cashResp proto.MinesCashRes
	if err := protobuf.Unmarshal(payload, &cashResp); err != nil {
		return
	}

	if cashResp.Code == 0 {
		atomic.AddInt64(&b.Stats.CashSuccess, 1)
		b.mu.Lock()
		b.Balance = cashResp.Balance
		b.PlayType = 0 // 重置下注状态
		b.mu.Unlock()
	} else {
		atomic.AddInt64(&b.Stats.CashFailed, 1)
	}
}

func (b *Bot) handleCancelBetResponse(payload []byte) {
	var cancelResp proto.MinesCancelBetRes
	if err := protobuf.Unmarshal(payload, &cancelResp); err != nil {
		return
	}

	if cancelResp.Code == 0 {
		atomic.AddInt64(&b.Stats.CancelBetSuccess, 1)
		b.mu.Lock()
		b.Balance = cancelResp.Balance
		b.PlayType = 0 // 重置下注状态
		b.mu.Unlock()
	} else {
		atomic.AddInt64(&b.Stats.CancelBetFailed, 1)
	}
}

// RunBot 运行单个机器人
func RunBot(bot *Bot, wg *sync.WaitGroup) {
	defer wg.Done()

	conn, err := bot.Login()
	if err != nil {
		log.Printf("Bot %d 登录失败: %v", bot.ID, err)
		atomic.AddInt64(&bot.Stats.TotalErrors, 1)
		return
	}
	defer conn.Close()

	log.Printf("Bot %d (%s) 登录成功", bot.ID, bot.Loginname)

	// 启动心跳
	go func() {
		ticker := time.NewTicker(10 * time.Second)
		defer ticker.Stop()
		
		for {
			select {
			case <-ticker.C:
				if !bot.IsActive {
					return
				}
				heartbeat := &proto.HeartBeat{
					Timestamp: fmt.Sprintf("%d", time.Now().UnixMilli()),
				}
				data, _ := protobuf.Marshal(heartbeat)
				msgData := Pack(0x11000, bot.SessionID, data)
				conn.WriteMessage(websocket.BinaryMessage, msgData)
			}
		}
	}()

	// 消息处理循环
	for bot.IsActive {
		conn.SetReadDeadline(time.Now().Add(30 * time.Second))
		_, msgData, err := conn.ReadMessage()
		if err != nil {
			if bot.IsActive {
				atomic.AddInt64(&bot.Stats.TotalErrors, 1)
			}
			break
		}

		msgID, _, payload, err := Unpack(msgData)
		if err != nil {
			continue
		}

		// 过滤心跳响应
		if msgID != 0x11000 {
			bot.HandleMessage(conn, msgID, payload)
		}
	}
}

// PrintStats 打印统计信息
func PrintStats() {
	ticker := time.NewTicker(10 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			stats := *globalStats
			fmt.Printf("\n=== 压测统计 (时间: %s) ===\n", time.Now().Format("15:04:05"))
			fmt.Printf("总机器人数: %d\n", stats.TotalBots)
			fmt.Printf("登录成功: %d, 登录失败: %d\n", stats.LoginSuccess, stats.LoginFailed)
			fmt.Printf("下注成功: %d, 下注失败: %d\n", stats.BetSuccess, stats.BetFailed)
			fmt.Printf("提现成功: %d, 提现失败: %d\n", stats.CashSuccess, stats.CashFailed)
			fmt.Printf("取消下注成功: %d, 取消下注失败: %d\n", stats.CancelBetSuccess, stats.CancelBetFailed)
			fmt.Printf("总错误数: %d\n", stats.TotalErrors)

			// 计算成功率
			if stats.LoginSuccess+stats.LoginFailed > 0 {
				loginRate := float64(stats.LoginSuccess) / float64(stats.LoginSuccess+stats.LoginFailed) * 100
				fmt.Printf("登录成功率: %.2f%%\n", loginRate)
			}
			if stats.BetSuccess+stats.BetFailed > 0 {
				betRate := float64(stats.BetSuccess) / float64(stats.BetSuccess+stats.BetFailed) * 100
				fmt.Printf("下注成功率: %.2f%%\n", betRate)
			}
			fmt.Printf("==============================\n")
		}
	}
}

func main() {
	// 解析命令行参数
	botCount := flag.Int("bots", 10, "机器人数量 (1-5000)")
	serverAddr := flag.String("server", "ws://127.0.0.1:5600/ws", "服务器地址")
	betMin := flag.Float64("bet-min", 10.0, "最小下注金额")
	betMax := flag.Float64("bet-max", 100.0, "最大下注金额")
	cancelRatio := flag.Float64("cancel-ratio", 0.5, "取消下注比例 (0.0-1.0)")
	flag.Parse()

	// 验证参数
	if *botCount < 1 || *botCount > 5000 {
		log.Fatal("机器人数量必须在1-5000之间")
	}
	if *cancelRatio < 0.0 || *cancelRatio > 1.0 {
		log.Fatal("取消下注比例必须在0.0-1.0之间")
	}

	config = Config{
		BotCount:       *botCount,
		ServerAddr:     *serverAddr,
		BetAmountMin:   *betMin,
		BetAmountMax:   *betMax,
		CancelBetRatio: *cancelRatio,
	}

	atomic.StoreInt64(&globalStats.TotalBots, int64(*botCount))

	log.Printf("=== 扫雷游戏压测启动 ===")
	log.Printf("机器人数量: %d", config.BotCount)
	log.Printf("服务器地址: %s", config.ServerAddr)
	log.Printf("下注金额范围: %.2f - %.2f", config.BetAmountMin, config.BetAmountMax)
	log.Printf("取消下注比例: %.2f", config.CancelBetRatio)
	log.Printf("=======================")

	// 创建机器人
	bots = make([]*Bot, config.BotCount)
	for i := 0; i < config.BotCount; i++ {
		bots[i] = NewBot(i+1, globalStats)
	}

	// 启动统计打印
	go PrintStats()

	// 启动机器人
	var wg sync.WaitGroup
	for i := 0; i < config.BotCount; i++ {
		wg.Add(1)
		go RunBot(bots[i], &wg)
		
		// 避免同时大量连接
		if i > 0 && i%50 == 0 {
			time.Sleep(1 * time.Second)
		} else {
			time.Sleep(100 * time.Millisecond)
		}
	}

	// 监听退出信号
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		<-sigChan
		log.Println("\n收到退出信号，正在停止所有机器人...")
		
		// 停止所有机器人
		for _, bot := range bots {
			bot.IsActive = false
		}
		
		// 等待一段时间后强制退出
		time.Sleep(5 * time.Second)
		os.Exit(0)
	}()

	// 等待所有机器人完成
	wg.Wait()
	log.Println("所有机器人已停止")
}