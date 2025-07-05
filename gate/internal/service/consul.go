package service

import (
	"gate/internal/interfaces"
	"gate/internal/model"
	"gate/internal/util"
	"reflect"
	"time"

	"github.com/hashicorp/consul/api"
)

type ConsulService struct {
	client            *api.Client
	serviceName       string
	queryInterval     int
	manager           *model.GameServerManager
	gameServerHandler interfaces.GameServerHandler
	lastServers       map[string]*model.GameServer // 用于检测变化
}

func NewConsulService(address, serviceName string, queryInterval int, manager *model.GameServerManager, gameServerHandler interfaces.GameServerHandler) (*ConsulService, error) {
	config := api.DefaultConfig()
	config.Address = address
	client, err := api.NewClient(config)
	if err != nil {
		return nil, err
	}
	return &ConsulService{
		client:            client,
		serviceName:       serviceName,
		queryInterval:     queryInterval,
		manager:           manager,
		gameServerHandler: gameServerHandler,
		lastServers:       make(map[string]*model.GameServer),
	}, nil
}

// StartBlockingQuery 使用 Blocking Query 低延迟更新游戏服务器列表
func (cs *ConsulService) StartBlockingQuery() {
	var lastIndex uint64
	for {
		services, meta, err := cs.client.Health().Service(cs.serviceName, "", true, &api.QueryOptions{
			WaitIndex: lastIndex,
			WaitTime:  time.Duration(cs.queryInterval) * time.Second,
		})
		if err != nil {
			util.Logger.Errorf("query consul service %s failed: %v", cs.serviceName, err)
			time.Sleep(5 * time.Second)
			continue
		}
		lastIndex = meta.LastIndex

		newServers := make(map[string]*model.GameServer)
		oldServers := cs.manager.GetAllServers()

		// 检测是否有变化
		hasChanges := false

		// 构建新服务器列表
		for _, entry := range services {
			serverID := entry.Service.ID
			// 使用所有tags作为支持的游戏类型
			gameTypes := entry.Service.Tags

			// 检查是否是新服务器或配置有变化
			if lastServer, exists := cs.lastServers[serverID]; !exists ||
				!reflect.DeepEqual(lastServer.GameTypes, gameTypes) ||
				lastServer.IP != entry.Service.Address ||
				lastServer.Port != entry.Service.Port {
				hasChanges = true
			}

			// 复用已有连接或创建新连接
			if oldServer, exists := oldServers[serverID]; exists {
				oldServer.GameTypes = gameTypes // 更新支持的游戏类型
				oldServer.Load = int32(entry.Service.Weights.Passing)
				oldServer.UpdatedAt = time.Now()
				newServers[serverID] = oldServer
				if hasChanges {
					util.Logger.Infof("Updated existing game server: %s (supports: %v)", serverID, gameTypes)
				}
			} else {
				hasChanges = true
				newServer := &model.GameServer{
					ID:        serverID,
					IP:        entry.Service.Address,
					Port:      entry.Service.Port,
					GameTypes: gameTypes,
					Load:      int32(entry.Service.Weights.Passing),
				}

				if err := newServer.Connect(); err != nil {
					util.Logger.Errorf("failed to connect to game server %s:%d: %v",
						newServer.IP, newServer.Port, err)
					continue
				}

				// 启动消息处理协程
				go cs.gameServerHandler.HandleGameServerMessage(newServer)
				newServers[serverID] = newServer
				util.Logger.Infof("Added new game server: %s (supports: %v)", serverID, gameTypes)
			}
		}

		// 检查是否有服务器下线
		for id := range cs.lastServers {
			found := false
			for _, entry := range services {
				if entry.Service.ID == id {
					found = true
					break
				}
			}
			if !found {
				hasChanges = true
				util.Logger.Infof("Game server %s went offline", id)
			}
		}

		// 关闭已下线的服务器连接
		for id, oldServer := range oldServers {
			if _, exists := newServers[id]; !exists {
				if oldServer.Conn != nil {
					oldServer.Conn.Close()
					util.Logger.Infof("Closed connection to offline server: %s", id)
				}
			}
		}

		cs.manager.UpdateServers(newServers)

		// 只在有变化时打印详细信息
		if hasChanges {
			util.Logger.Infof("========== Consul Service Discovery Update ==========")
			util.Logger.Infof("Found %d %s service instances from Consul", len(services), cs.serviceName)

			for i, entry := range services {
				util.Logger.Infof("Service Instance #%d:", i+1)
				util.Logger.Infof("  Service ID: %s", entry.Service.ID)
				util.Logger.Infof("  Service Name: %s", entry.Service.Service)
				util.Logger.Infof("  Address: %s:%d", entry.Service.Address, entry.Service.Port)
				util.Logger.Infof("  Weight: %d", entry.Service.Weights.Passing)
				util.Logger.Infof("  Supported Game Types (Tags): %v", entry.Service.Tags)
				util.Logger.Infof("  Health Check Status: %s", entry.Checks.AggregatedStatus())
			}

			// 打印最终的游戏类型映射表
			cs.printGameTypeMapping(newServers)
			util.Logger.Infof("=====================================================")

			// 更新最后的服务器状态用于下次比较
			cs.lastServers = make(map[string]*model.GameServer)
			for id, server := range newServers {
				cs.lastServers[id] = &model.GameServer{
					ID:        server.ID,
					IP:        server.IP,
					Port:      server.Port,
					GameTypes: append([]string{}, server.GameTypes...), // 深拷贝
					Load:      server.Load,
				}
			}
		}
	}
}

// printGameTypeMapping 打印游戏类型到服务器的映射关系
func (cs *ConsulService) printGameTypeMapping(servers map[string]*model.GameServer) {
	util.Logger.Infof("===== Game Type to Server Mapping =====")

	// 构建 游戏类型 -> 服务器列表 的映射
	gameTypeMapping := make(map[string][]string)

	for serverID, server := range servers {
		for _, gameType := range server.GameTypes {
			if gameTypeMapping[gameType] == nil {
				gameTypeMapping[gameType] = make([]string, 0)
			}
			gameTypeMapping[gameType] = append(gameTypeMapping[gameType], serverID)
		}
	}

	if len(gameTypeMapping) == 0 {
		util.Logger.Warnf("No game type mappings found!")
		return
	}

	for gameType, serverIDs := range gameTypeMapping {
		util.Logger.Infof("🎮 Game Type '%s' -> Servers: %v", gameType, serverIDs)
	}

	util.Logger.Infof("Total supported game types: %d", len(gameTypeMapping))
}

// 修改ConnectToGameServer增加重试逻辑
func ConnectToGameServer(server *model.GameServer) error {
	return server.Connect()
}
