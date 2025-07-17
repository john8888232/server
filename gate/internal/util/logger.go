package util

import (
	"gate/internal/config"
	"io"
	"os"

	"github.com/sirupsen/logrus"
)

var Logger *logrus.Logger

func InitLogger(cfg *config.Config) {
	Logger = logrus.New()
	level, err := logrus.ParseLevel(cfg.Logger.Level)
	if err != nil {
		level = logrus.InfoLevel
	}
	Logger.SetLevel(level)
	file, _ := os.OpenFile(cfg.Logger.File, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	multiWriter := io.MultiWriter(os.Stdout, file)
	Logger.SetOutput(multiWriter)
	Logger.SetFormatter(&logrus.TextFormatter{
		FullTimestamp: true,
		DisableQuote:  true,
	})
}
