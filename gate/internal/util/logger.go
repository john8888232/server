package util

import (
	"fmt"
	"gate/internal/config"
	"io"
	"os"
	"path/filepath"
	"time"

	"github.com/sirupsen/logrus"
	"gopkg.in/natefinch/lumberjack.v2"
)

// LoggerConfig 通用日志配置接口
type LoggerConfig interface {
	GetLogLevel() string
	GetLogFile() string
}

// ConfigWrapper 包装器，用于让 config.Config 实现 LoggerConfig 接口
type ConfigWrapper struct {
	*config.Config
}

func (c *ConfigWrapper) GetLogLevel() string {
	return c.Logger.Level
}

func (c *ConfigWrapper) GetLogFile() string {
	return c.Logger.File
}

// StubConfig 为 gameserver_stub 提供的配置结构
type StubConfig struct {
	Logger struct {
		Level string
		File  string
	}
}

func (c *StubConfig) GetLogLevel() string {
	return c.Logger.Level
}

func (c *StubConfig) GetLogFile() string {
	return c.Logger.File
}

var Logger *logrus.Logger

// CustomRotatingWriter 自定义日志轮转器
type CustomRotatingWriter struct {
	*lumberjack.Logger
	baseDir  string
	baseName string
}

// NewCustomRotatingWriter 创建自定义轮转写入器
func NewCustomRotatingWriter(logPath string, maxSize int) *CustomRotatingWriter {
	dir := filepath.Dir(logPath)
	name := filepath.Base(logPath)

	// 移除扩展名，获取基础名称
	if ext := filepath.Ext(name); ext != "" {
		name = name[:len(name)-len(ext)]
	}

	// 创建日志目录
	if err := os.MkdirAll(dir, 0755); err != nil {
		fmt.Printf("Failed to create log directory %s: %v\n", dir, err)
	}

	// 生成带时间戳的文件名
	timestamp := time.Now().Format("060102_150405") // YYMMDD_HHMMSS
	filename := fmt.Sprintf("%s_%s.log", name, timestamp)
	fullPath := filepath.Join(dir, filename)

	crw := &CustomRotatingWriter{
		baseDir:  dir,
		baseName: name,
		Logger: &lumberjack.Logger{
			Filename:   fullPath,
			MaxSize:    maxSize, // MB
			MaxBackups: 30,      // 保留30个备份文件
			MaxAge:     7,       // 保留7天
			Compress:   true,    // 压缩旧文件
			LocalTime:  true,    // 使用本地时间
		},
	}

	return crw
}

// Write 实现 io.Writer 接口，添加自定义轮转逻辑
func (crw *CustomRotatingWriter) Write(p []byte) (n int, err error) {
	// 检查是否需要轮转（基于日期）
	if crw.shouldRotateByDate() {
		if err := crw.rotateByDate(); err != nil {
			fmt.Printf("Failed to rotate log by date: %v\n", err)
		}
	}

	// 使用 lumberjack 的 Write 方法，它会自动处理大小轮转
	return crw.Logger.Write(p)
}

// shouldRotateByDate 检查是否需要按日期轮转
func (crw *CustomRotatingWriter) shouldRotateByDate() bool {
	if crw.Logger.Filename == "" {
		return false
	}

	// 获取文件信息
	info, err := os.Stat(crw.Logger.Filename)
	if err != nil {
		return false
	}

	// 检查文件创建日期是否是今天
	fileDate := info.ModTime().Format("060102")
	todayDate := time.Now().Format("060102")

	return fileDate != todayDate
}

// rotateByDate 按日期轮转日志
func (crw *CustomRotatingWriter) rotateByDate() error {
	// 强制轮转当前文件
	if err := crw.Logger.Rotate(); err != nil {
		return err
	}

	// 生成新的文件名
	timestamp := time.Now().Format("060102_150405")
	filename := fmt.Sprintf("%s_%s.log", crw.baseName, timestamp)
	newPath := filepath.Join(crw.baseDir, filename)

	// 更新文件路径
	crw.Logger.Filename = newPath

	return nil
}

// InitLogger 初始化日志系统，支持按日期和大小轮转
func InitLogger(cfg LoggerConfig) {
	Logger = logrus.New()

	// 设置日志级别
	level, err := logrus.ParseLevel(cfg.GetLogLevel())
	if err != nil {
		level = logrus.InfoLevel
	}
	Logger.SetLevel(level)

	// 创建自定义轮转写入器，最大100MB
	rotatingWriter := NewCustomRotatingWriter(cfg.GetLogFile(), 100)

	// 设置多写入器：同时输出到控制台和文件
	multiWriter := io.MultiWriter(os.Stdout, rotatingWriter)
	Logger.SetOutput(multiWriter)

	// 设置日志格式
	Logger.SetFormatter(&logrus.TextFormatter{
		FullTimestamp:   true,
		DisableQuote:    true,
		TimestampFormat: "2006-01-02 15:04:05",
	})

	Logger.Infof("Logger initialized with rotating file: %s", rotatingWriter.Logger.Filename)
}
