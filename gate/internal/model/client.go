package model

import (
	"sync"

	"github.com/gorilla/websocket"
)

// Client 表示一个客户端连接
type Client struct {
	Conn       *websocket.Conn
	SessionID  string
	GameServer *GameServer // 关联的游戏服务器
}

// ClientManager 管理客户端连接，支持并发安全
type ClientManager struct {
	clients map[string]*Client // sessionID -> Client
	mutex   sync.RWMutex
}

func NewClientManager() *ClientManager {
	return &ClientManager{
		clients: make(map[string]*Client),
	}
}

func (cm *ClientManager) AddClient(sessionID string, client *Client) {
	cm.mutex.Lock()
	defer cm.mutex.Unlock()
	cm.clients[sessionID] = client
}

func (cm *ClientManager) RemoveClient(sessionID string) {
	cm.mutex.Lock()
	defer cm.mutex.Unlock()
	delete(cm.clients, sessionID)
}

func (cm *ClientManager) GetClient(sessionID string) (*Client, bool) {
	cm.mutex.RLock()
	defer cm.mutex.RUnlock()
	client, exists := cm.clients[sessionID]
	return client, exists
}

func (cm *ClientManager) Len() int {
	cm.mutex.RLock()
	defer cm.mutex.RUnlock()
	return len(cm.clients)
}
