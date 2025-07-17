package config

import (
	"log"
	"os"

	"gopkg.in/yaml.v3"
)

type Config struct {
	Gateway struct {
		Host           string `yaml:"host"`
		WSPort         int    `yaml:"ws_port"`
		TCPPort        int    `yaml:"tcp_port"`
		MaxConnections int    `yaml:"max_connections"`
		ReadTimeout    int    `yaml:"read_timeout"`
		WriteTimeout   int    `yaml:"write_timeout"`
		PProfPort      int    `yaml:"pprof_port"`
	} `yaml:"gateway"`
	Consul struct {
		Address       string `yaml:"address"`
		ServiceName   string `yaml:"service_name"`
		QueryInterval int    `yaml:"query_interval"`
	} `yaml:"consul"`
	Redis struct {
		Address  string `yaml:"address"`
		Password string `yaml:"password"`
		DB       int    `yaml:"db"`
		PoolSize int    `yaml:"pool_size"`
	} `yaml:"redis"`
	Logger struct {
		Level string `yaml:"level"`
		File  string `yaml:"file"`
	} `yaml:"logger"`
}

var AppConfig Config

func LoadConfig(filePath string) error {
	data, err := os.ReadFile(filePath)
	if err != nil {
		return err
	}
	err = yaml.Unmarshal(data, &AppConfig)
	if err != nil {
		return err
	}
	log.Printf("Configuration loaded: %+v\n", AppConfig)
	return nil
}
