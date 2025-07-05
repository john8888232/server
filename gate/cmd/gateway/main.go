package main

import (
	"fmt"
	"gate/internal/config"
	"gate/internal/handler"
	"gate/internal/model"
	"gate/internal/service"
	"gate/internal/util"
	"log"
	"net/http"
	_ "net/http/pprof" // 必须添加此行
	"os"
	"os/signal"
	"syscall"
)

func main() {
	// 加载配置
	if err := config.LoadConfig("config.yaml"); err != nil {
		log.Fatalf("load config file failed: %v", err)
	}
	util.InitLogger(&config.AppConfig)
	// 初始化组件
	clientMgr := model.NewClientManager()
	gameServerMgr := model.NewGameServerManager()
	redisSvc := service.NewRedisService(
		config.AppConfig.Redis.Address,
		config.AppConfig.Redis.Password,
		config.AppConfig.Redis.DB,
		config.AppConfig.Redis.PoolSize,
	)

	// 初始化处理器
	gameServerHandler := handler.NewGameServerHandler(&config.AppConfig, clientMgr)

	// 初始化Consul服务
	consulSvc, err := service.NewConsulService(
		config.AppConfig.Consul.Address,
		config.AppConfig.Consul.ServiceName,
		config.AppConfig.Consul.QueryInterval,
		gameServerMgr,
		gameServerHandler,
	)
	if err != nil {
		util.Logger.Fatalf("consul init failed: %v", err)
	}

	// 启动服务
	go consulSvc.StartBlockingQuery()

	// 初始化客户端处理器
	clientHandler := handler.NewClientHandler(&config.AppConfig, clientMgr, gameServerMgr, redisSvc)

	// 启动 WebSocket 服务 (Client -> Gateway)
	wsAddr := fmt.Sprintf("%s:%d", config.AppConfig.Gateway.Host, config.AppConfig.Gateway.WSPort)
	http.HandleFunc("/ws", clientHandler.ServeWS)
	util.Logger.Infof("Starting WebSocket server on %s", wsAddr)
	go func() {
		if err := http.ListenAndServe(wsAddr, nil); err != nil {
			util.Logger.Fatalf("Failed to start WebSocket server: %v", err)
		}
	}()
	// 启动 pprof HTTP 服务，用于性能监控
	pprofAddr := fmt.Sprintf("%s:%d", config.AppConfig.Gateway.Host, config.AppConfig.Gateway.PProfPort)
	go func() {
		log.Printf("pprof server starting on %s", pprofAddr)
		if err := http.ListenAndServe(pprofAddr, nil); err != nil {
			log.Fatalf("pprof server failed: %v", err)
		}
	}()
	stopChan := make(chan os.Signal, 1)
	signal.Notify(stopChan, os.Interrupt, syscall.SIGTERM)
	<-stopChan
	util.Logger.Info("Shutting down gracefully...")
}
