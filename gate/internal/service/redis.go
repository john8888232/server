package service

import (
	"context"
	"gate/internal/util"

	"github.com/redis/go-redis/v9"
)

type RedisService struct {
	client *redis.Client
}

func NewRedisService(address, password string, db, poolSize int) *RedisService {
	client := redis.NewClient(&redis.Options{
		Addr:     address,
		Password: password,
		DB:       db,
		PoolSize: poolSize,
	})
	return &RedisService{client: client}
}

// GetPlayerGameServer 获取玩家所在的游戏服务器
func (rs *RedisService) GetPlayerGameServer(ctx context.Context, loginname string) (string, error) {
	key := rs.getPlayerKey(loginname)
	val, err := rs.client.Get(ctx, key).Result()
	if err == redis.Nil {
		return "", nil
	} else if err != nil {
		util.Logger.Errorf("Failed to get player game server from Redis: %v", err)
		return "", err
	}
	return val, nil
}

func (rs *RedisService) getPlayerKey(loginname string) string {
	return "player:" + loginname + ":onlineInfo"
}
