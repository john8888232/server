#!/bin/bash

# Gate项目编译脚本
# 用于编译cmd目录下的各个Go程序到对应的bin目录

set -e  # 遇到错误立即退出

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 定义颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查Go环境
check_go() {
    if ! command -v go &> /dev/null; then
        print_error "Go is not installed or not in PATH"
        exit 1
    fi
    print_info "Go version: $(go version)"
}

# 编译单个程序
build_program() {
    local program_name=$1
    local source_dir="cmd/$program_name"
    local bin_dir="$source_dir/bin"
    local binary_name=$program_name
    
    print_info "Building $program_name..."
    
    # 检查源目录是否存在
    if [[ ! -d "$source_dir" ]]; then
        print_warning "Source directory $source_dir does not exist, skipping..."
        return 0
    fi
    
    # 检查main.go文件是否存在
    if [[ ! -f "$source_dir/main.go" ]]; then
        print_warning "main.go not found in $source_dir, skipping..."
        return 0
    fi
    
    # 创建bin目录
    mkdir -p "$bin_dir"
    
    # 编译程序
    local output_path="$bin_dir/$binary_name"
    
    print_info "Compiling $source_dir/main.go -> $output_path"
    
    # 设置Linux环境变量并编译
    CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -o "$output_path" "$source_dir/main.go"
    
    if [[ $? -eq 0 ]]; then
        # 设置可执行权限
        chmod +x "$output_path"
        
        # 获取文件大小
        local file_size=$(du -h "$output_path" | cut -f1)
        print_success "Successfully built $binary_name ($file_size)"
    else
        print_error "Failed to build $program_name"
        return 1
    fi
}

# 清理二进制文件
clean() {
    print_info "Cleaning binary files..."
    
    for program in gameserver_stub gateway test; do
        local bin_file="cmd/$program/bin/$program"
        if [[ -f "$bin_file" ]]; then
            rm -f "$bin_file"
            print_info "Removed $bin_file"
        fi
    done
    
    print_success "Clean completed"
}

# 显示帮助信息
show_help() {
    echo "Gate项目编译脚本"
    echo ""
    echo "用法: $0 [选项] [程序名...]"
    echo ""
    echo "选项:"
    echo "  -h, --help     显示此帮助信息"
    echo "  -c, --clean    清理编译生成的二进制文件"
    echo "  -a, --all      编译所有程序 (默认行为)"
    echo ""
    echo "程序名:"
    echo "  gameserver_stub  编译游戏服务器存根"
    echo "  gateway          编译网关服务器"
    echo "  test            编译测试客户端"
    echo ""
    echo "示例:"
    echo "  $0                    # 编译所有程序"
    echo "  $0 gateway            # 只编译gateway"
    echo "  $0 gateway test       # 编译gateway和test"
    echo "  $0 --clean            # 清理所有二进制文件"
}

# 主函数
main() {
    local programs=()
    local clean_flag=false
    local build_all=true
    
    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            -c|--clean)
                clean_flag=true
                shift
                ;;
            -a|--all)
                build_all=true
                shift
                ;;
            gameserver_stub|gateway|test)
                programs+=("$1")
                build_all=false
                shift
                ;;
            *)
                print_error "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
    done
    
    # 执行清理操作
    if [[ "$clean_flag" == true ]]; then
        clean
        return 0
    fi
    
    # 检查Go环境
    check_go
    
    # 确定要编译的程序列表
    if [[ "$build_all" == true ]] || [[ ${#programs[@]} -eq 0 ]]; then
        programs=("gameserver_stub" "gateway" "test")
    fi
    
    print_info "Starting build process..."
    print_info "Target programs: ${programs[*]}"
    
    # 编译程序
    local success_count=0
    local total_count=${#programs[@]}
    
    for program in "${programs[@]}"; do
        if build_program "$program"; then
            ((success_count++))
        fi
    done
    
    # 显示编译结果
    echo ""
    print_info "Build Summary:"
    print_info "Successfully built: $success_count/$total_count programs"
    
    if [[ $success_count -eq $total_count ]]; then
        print_success "All programs built successfully!"
    else
        print_warning "Some programs failed to build."
        exit 1
    fi
    
    # 显示生成的文件
    echo ""
    print_info "Generated binaries:"
    for program in "${programs[@]}"; do
        local bin_file="cmd/$program/bin/$program"
        if [[ -f "$bin_file" ]]; then
            local file_size=$(du -h "$bin_file" | cut -f1)
            echo "  $bin_file ($file_size)"
        fi
    done
}

# 运行主函数
main "$@"
