package util

import (
	"bytes"
	"encoding/binary"
	"errors"
)

// 包头长度 (4 bytes for length + 4 bytes for msgid + 32 bytes for sessionid)
const HeaderLength = 40

// Pack 打包消息，格式: [length (4 bytes)][msgid (4 bytes)][sessionid (32 bytes)][data]
func Pack(msgID uint32, sessionID string, data []byte) []byte {
	sessionIDBytes := make([]byte, 32)
	copy(sessionIDBytes, []byte(sessionID)) // 不足 32 字节会自动填充 \x00，超过则截断

	totalLength := uint32(HeaderLength + len(data)) // length + msgid + sessionid + data

	result := make([]byte, totalLength)
	binary.BigEndian.PutUint32(result[0:4], totalLength) // length 字段包含自身长度
	binary.BigEndian.PutUint32(result[4:8], msgID)
	copy(result[8:40], sessionIDBytes) // 固定 32 字节的 sessionID
	copy(result[40:], data)
	return result
}

// Unpack 解包消息，处理粘包，返回解包后的 msgid, sessionid, 数据和剩余字节
func Unpack(buffer *bytes.Buffer) (uint32, string, []byte, error) {
	if buffer.Len() < 4 {
		return 0, "", nil, errors.New("buffer too short for length")
	}

	lengthBytes := buffer.Bytes()[:4]
	totalLength := binary.BigEndian.Uint32(lengthBytes)
	if buffer.Len() < int(totalLength) {
		return 0, "", nil, errors.New("buffer too short for full message")
	}

	// 读取 msgid
	msgID := binary.BigEndian.Uint32(buffer.Bytes()[4:8])

	// 读取 sessionid (固定 32 字节)
	sessionIDBytes := buffer.Bytes()[8:40]
	// 移除填充的 \x00 字节，转换为字符串
	sessionID := string(bytes.TrimRight(sessionIDBytes, "\x00"))

	// 读取数据部分
	dataStart := uint32(40)
	dataEnd := totalLength
	data := buffer.Bytes()[dataStart:dataEnd]

	// 移除已处理的数据
	buffer.Next(int(dataEnd))

	return msgID, sessionID, data, nil
}
