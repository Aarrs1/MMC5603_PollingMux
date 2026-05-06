import serial
import numpy as np
import pygame
import os

# 禁用垂直同步
os.environ['SDL_VSYNC'] = '0'

# 串口配置
SERIAL_PORT = 'COM4'  # 根据实际情况修改
BAUD_RATE = 921600

# 显示配置
CELL_SIZE = 60
GAP = 0
MARGIN = 40
LABEL_HEIGHT = 30

# 颜色
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
GRAY = (40, 40, 40)

def parse_data(line):
    """解析CSV数据行"""
    try:
        if line.startswith('F,'):
            line = line[2:]
        values = [int(x) for x in line.split(',') if x.strip()]
        if len(values) == 48:
            x_data = np.array([values[i*3] for i in range(16)]).reshape(4, 4)
            y_data = np.array([values[i*3+1] for i in range(16)]).reshape(4, 4)
            z_data = np.array([values[i*3+2] for i in range(16)]).reshape(4, 4)
            return x_data, y_data, z_data
    except Exception as e:
        print(f"解析错误: {e}")
    return None, None, None

def get_color(value):
    """根据归一化值(-1到1)返回颜色：红->黑->绿"""
    value = max(-1, min(1, value))
    if value < 0:
        # 负值：红色渐变 (红->黑)
        r = int(-value * 255)
        g = 0
    else:
        # 正值：绿色渐变 (黑->绿)
        r = 0
        g = int(value * 255)
    return (r, g, 0)

def draw_grid(surface, data, x_offset, y_offset, label, font):
    """绘制一个4x4网格"""
    # 绘制标签
    text = font.render(label, True, WHITE)
    text_rect = text.get_rect(center=(x_offset + CELL_SIZE * 2, y_offset - LABEL_HEIGHT // 2))
    surface.blit(text, text_rect)

    # 绘制网格
    for row in range(4):
        for col in range(4):
            rect = pygame.Rect(
                x_offset + col * (CELL_SIZE + GAP),
                y_offset + row * (CELL_SIZE + GAP),
                CELL_SIZE,
                CELL_SIZE
            )
            color = get_color(data[row][col])
            pygame.draw.rect(surface, color, rect)
            pygame.draw.rect(surface, GRAY, rect, 1)  # 边框

def main():
    pygame.init()

    # 计算窗口大小
    grid_width = 4 * CELL_SIZE + 3 * GAP
    total_width = 3 * grid_width + 4 * MARGIN
    total_height = 4 * CELL_SIZE + 3 * GAP + 2 * MARGIN + LABEL_HEIGHT

    screen = pygame.display.set_mode((total_width, total_height), vsync=0)
    pygame.display.set_caption('MMC5603 传感器矩阵显示')

    font = pygame.font.SysFont('SimHei', 20)
    clock = pygame.time.Clock()

    # 连接串口
    ser = None
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.05)
        print(f"已连接到 {SERIAL_PORT}")
    except Exception as e:
        print(f"无法连接串口: {e}")
        return

    max_val = (1 << 20) - 1
    center = max_val / 2

    # 初始数据
    x_norm = np.zeros((4, 4))
    y_norm = np.zeros((4, 4))
    z_norm = np.zeros((4, 4))

    # TPS统计
    tps_count = 0
    tps_value = 0
    tps_timer = pygame.time.get_ticks()

    print("按 ESC 或关闭窗口退出")

    running = True
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False

        # 读取串口数据
        if ser and ser.is_open:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line.startswith('F,'):
                x_data, y_data, z_data = parse_data(line)
                if x_data is not None:
                    x_norm = (x_data - center) / center
                    y_norm = (y_data - center) / center
                    z_norm = (z_data - center) / center
                    tps_count += 1

        # 每秒更新TPS
        current_time = pygame.time.get_ticks()
        if current_time - tps_timer >= 1000:
            tps_value = tps_count
            tps_count = 0
            tps_timer = current_time

        # 绘制
        screen.fill(BLACK)

        # 显示FPS和TPS
        fps = clock.get_fps()
        fps_text = font.render(f'FPS: {fps:.1f}', True, WHITE)
        screen.blit(fps_text, (10, 10))
        tps_text = font.render(f'TPS: {tps_value}', True, WHITE)
        screen.blit(tps_text, (10, 35))

        # 三个网格的X偏移
        x1 = MARGIN
        x2 = MARGIN + grid_width + MARGIN
        x3 = MARGIN + 2 * (grid_width + MARGIN)
        y = MARGIN + LABEL_HEIGHT

        draw_grid(screen, x_norm, x1, y, 'X轴', font)
        draw_grid(screen, y_norm, x2, y, 'Y轴', font)
        draw_grid(screen, z_norm, x3, y, 'Z轴', font)

        pygame.display.flip()
        clock.tick(60)  # 限制60FPS，减少空转

    if ser and ser.is_open:
        ser.close()
    pygame.quit()

if __name__ == '__main__':
    main()