package model

import (
	"fmt"
	"gate/internal/util"
	"net"
	"sync"
	"time"
)

// GameServer 表示一个游戏服务器
type GameServer struct {
	ID        string
	IP        string
	Port      int
	GameTypes []string // 支持的游戏类型列表
	Load      int32    // 当前负载（连接数）
	Conn      *net.TCPConn
	UpdatedAt time.Time // 最后更新时间
}

// GameServerManager 管理游戏服务器列表，支持并发安全
type GameServerManager struct {
	servers map[string]*GameServer // ID -> GameServer
	mutex   sync.RWMutex
}

func NewGameServerManager() *GameServerManager {
	return &GameServerManager{
		servers: make(map[string]*GameServer),
	}
}

func (gsm *GameServerManager) UpdateServers(newServers map[string]*GameServer) {
	gsm.mutex.Lock()
	defer gsm.mutex.Unlock()

	// 关闭被移除的服务器连接
	for id, oldServer := range gsm.servers {
		if _, exists := newServers[id]; !exists {
			if oldServer.Conn != nil {
				oldServer.Conn.Close()
				util.Logger.Infof("close game server connection: %s", id)
			}
		}
	}

	// 更新服务器列表
	gsm.servers = make(map[string]*GameServer)
	for id, server := range newServers {
		gsm.servers[id] = server
	}
}

func (gsm *GameServerManager) GetServersByGameType(gameType string) []*GameServer {
	gsm.mutex.RLock()
	defer gsm.mutex.RUnlock()
	var result []*GameServer

	util.Logger.Infof("Searching for servers supporting game type '%s'...", gameType)

	for serverID, server := range gsm.servers {
		// 检查服务器是否支持指定的游戏类型
		for _, supportedType := range server.GameTypes {
			if supportedType == gameType {
				result = append(result, server)
				util.Logger.Infof("  Found matching server: %s (supports: %v)", serverID, server.GameTypes)
				break
			}
		}
	}

	if len(result) == 0 {
		util.Logger.Warnf("  No servers found supporting game type '%s'", gameType)
		util.Logger.Infof("  Current servers and their supported game types:")
		for serverID, server := range gsm.servers {
			util.Logger.Infof("    Server %s: %v", serverID, server.GameTypes)
		}
	} else {
		util.Logger.Infof("  Found %d servers supporting game type '%s'", len(result), gameType)
	}

	return result
}

// GetGameServerByID 根据服务器ID获取游戏服务器
func (gsm *GameServerManager) GetServerByID(id string) *GameServer {
	gsm.mutex.RLock()
	defer gsm.mutex.RUnlock()
	return gsm.servers[id]
}

func (gsm *GameServerManager) SelectServerByLoad(gameType string) *GameServer {
	util.Logger.Infof("Starting optimal server selection for game type '%s'...", gameType)

	servers := gsm.GetServersByGameType(gameType)
	if len(servers) == 0 {
		util.Logger.Warnf("No available servers support game type '%s'", gameType)
		return nil
	}

	var selected *GameServer
	minLoad := int32(1<<31 - 1) // 最大 int32

	util.Logger.Infof("Starting load balancing selection, candidate servers:")
	for _, server := range servers {
		util.Logger.Infof("  Server %s: load=%d", server.ID, server.Load)
		if server.Load < minLoad {
			minLoad = server.Load
			selected = server
		}
	}

	if selected != nil {
		util.Logger.Infof("Selected server: %s (load: %d)", selected.ID, selected.Load)
	} else {
		util.Logger.Errorf("Load balancing selection failed")
	}

	return selected
}

// GetAllServers 获取当前所有游戏服务器副本
func (gsm *GameServerManager) GetAllServers() map[string]*GameServer {
	gsm.mutex.RLock()
	defer gsm.mutex.RUnlock()
	serversCopy := make(map[string]*GameServer)
	for id, server := range gsm.servers {
		serversCopy[id] = server
	}
	return serversCopy
}

func (gs *GameServer) Connect() error {
	addr := net.JoinHostPort(gs.IP, fmt.Sprintf("%d", gs.Port))

	// 带超时的连接
	conn, err := net.DialTimeout("tcp", addr, 3*time.Second)
	if err != nil {
		return fmt.Errorf("connect to game server %s:%d failed: %v",
			gs.IP, gs.Port, err)
	}

	tcpConn, ok := conn.(*net.TCPConn)
	if !ok {
		return fmt.Errorf("non-TCP connection")
	}

	// 设置KeepAlive
	tcpConn.SetKeepAlive(true)
	tcpConn.SetKeepAlivePeriod(30 * time.Second)

	gs.Conn = tcpConn
	return nil
}
