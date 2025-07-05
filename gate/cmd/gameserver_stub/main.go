package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"time"

	"gate/internal/util"
	"gate/proto"

	"github.com/hashicorp/consul/api"
	"github.com/redis/go-redis/v9"
	protobuf "google.golang.org/protobuf/proto"
	"gopkg.in/yaml.v3"
)

// Config 结构体用于存储配置文件内容
type Config struct {
	GameServer struct {
		ID        string   `yaml:"id"`
		Name      string   `yaml:"name"`
		GameTypes []string `yaml:"game_types"`
		IP        string   `yaml:"ip"`
		Port      int      `yaml:"port"`
	} `yaml:"gameserver"`
	Consul struct {
		Address string `yaml:"address"`
	} `yaml:"consul"`
	Redis struct {
		Address  string `yaml:"address"`
		Password string `yaml:"password"`
		DB       int    `yaml:"db"`
	} `yaml:"redis"`
}

var (
	configPath = flag.String("config", "config.yaml", "Path to configuration file")
	cfg        Config
)

func main() {
	flag.Parse()

	// 加载配置文件
	if err := loadConfig(*configPath); err != nil {
		log.Fatalf("Failed to load config file: %v", err)
	}

	// 注册到 Consul
	if err := registerToConsul(); err != nil {
		log.Fatalf("Failed to register to Consul: %v", err)
	}

	// 启动 TCP 服务器
	listenAddr := fmt.Sprintf("%s:%d", cfg.GameServer.IP, cfg.GameServer.Port)
	listener, err := net.Listen("tcp", listenAddr)
	if err != nil {
		log.Fatalf("Failed to listen on %s: %v", listenAddr, err)
	}
	defer listener.Close()

	log.Printf("GameServer Stub listening on %s, ID: %s, GameTypes: %v", listenAddr, cfg.GameServer.ID, cfg.GameServer.GameTypes)

	// 接受连接
	for {
		conn, err := listener.Accept()
		if err != nil {
			log.Printf("Failed to accept connection: %v", err)
			continue
		}
		log.Printf("New connection from %s", conn.RemoteAddr().String())
		go handleConnection(conn)
	}
}

// loadConfig 从 YAML 文件加载配置
func loadConfig(path string) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	return yaml.Unmarshal(data, &cfg)
}

// registerToConsul 将服务注册到 Consul
func registerToConsul() error {
	consulConfig := api.DefaultConfig()
	consulConfig.Address = cfg.Consul.Address
	client, err := api.NewClient(consulConfig)
	if err != nil {
		return err
	}

	// 服务注册信息
	registration := &api.AgentServiceRegistration{
		ID:      cfg.GameServer.ID,
		Name:    cfg.GameServer.Name,
		Address: cfg.GameServer.IP,
		Port:    cfg.GameServer.Port,
		Tags:    cfg.GameServer.GameTypes,
		Weights: &api.AgentWeights{Passing: 10}, // 模拟负载
		Check: &api.AgentServiceCheck{
			TCP:      fmt.Sprintf("%s:%d", cfg.GameServer.IP, cfg.GameServer.Port),
			Interval: "10s",
			Timeout:  "2s",
		},
	}

	// 注册服务
	err = client.Agent().ServiceRegister(registration)
	if err != nil {
		return err
	}
	log.Printf("Successfully registered to Consul with ID: %s", cfg.GameServer.ID)
	return nil
}

// handleConnection 处理单个 TCP 连接
func handleConnection(conn net.Conn) {
	defer conn.Close()

	buffer := make([]byte, 1024)
	dataBuffer := make([]byte, 0)

	for {
		n, err := conn.Read(buffer)
		if err != nil {
			if err != io.EOF {
				log.Printf("Failed to read from connection %s: %v", conn.RemoteAddr().String(), err)
			}
			return
		}

		dataBuffer = append(dataBuffer, buffer[:n]...)

		// 处理粘包
		for len(dataBuffer) >= util.HeaderLength {
			length := binary.BigEndian.Uint32(dataBuffer[:4])
			if len(dataBuffer) < int(length)+4 {
				break // 数据不足，继续读取
			}

			msgID := binary.BigEndian.Uint32(dataBuffer[4:8])
			sessionIDBytes := dataBuffer[8:40]
			sessionID := string(bytes.TrimRight(sessionIDBytes, "\x00"))
			msgData := dataBuffer[40 : 4+length]

			if msgID == 0x11001 { // LoginReq
				handleLoginRequest(conn, sessionID, msgData)
			} else {
				log.Printf("Received non-login message with ID %d from session %s", msgID, sessionID)
			}

			dataBuffer = dataBuffer[4+length:] // 移除已处理的数据
		}
	}
}

// handleLoginRequest 处理登录请求并回复
func handleLoginRequest(conn net.Conn, sessionID string, data []byte) {
	var loginReq proto.LoginReq
	if err := protobuf.Unmarshal(data, &loginReq); err != nil {
		log.Printf("Failed to unmarshal LoginReq: %v", err)
		return
	}

	log.Printf("Received LoginReq for user %s, session %s", loginReq.Loginname, sessionID)

	// 模拟登录成功
	resp := &proto.LoginResp{
		Loginname: loginReq.Loginname,
		Code:      0, // 成功
		Message:   "Login successful",
	}
	respData, err := protobuf.Marshal(resp)
	if err != nil {
		log.Printf("Failed to marshal LoginResp: %v", err)
		return
	}

	// 打包并发送回复
	packedData := util.Pack(0x11002, sessionID, respData)
	_, err = conn.Write(packedData)
	if err != nil {
		log.Printf("Failed to send LoginResp to session %s: %v", sessionID, err)
		return
	}

	// 将 loginname -> gameserverID 映射写入 Redis
	if err := writeToRedis(loginReq.Loginname, cfg.GameServer.ID); err != nil {
		log.Printf("Failed to write to Redis for user %s: %v", loginReq.Loginname, err)
	}
}

// writeToRedis 将用户与游戏服务器的映射写入 Redis
func writeToRedis(loginname, gameServerID string) error {
	client := redis.NewClient(&redis.Options{
		Addr:         cfg.Redis.Address,
		Password:     cfg.Redis.Password,
		DB:           cfg.Redis.DB,
		PoolSize:     100,
		MinIdleConns: 10,
		PoolTimeout:  4 * time.Second,
	})
	defer client.Close()

	ctx := context.Background()
	key := fmt.Sprintf("player:%s:onlineInfo", loginname)
	err := client.Set(ctx, key, gameServerID, 3*time.Minute).Err()
	if err != nil {
		return err
	}
	log.Printf("Wrote user %s mapping to game server %s in Redis", loginname, gameServerID)
	return nil
}
