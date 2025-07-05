package interfaces

import "gate/internal/model"

type GameServerHandler interface {
	HandleGameServerMessage(gs *model.GameServer)
}
