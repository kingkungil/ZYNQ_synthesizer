# LED

set_property -dict {PACKAGE_PIN P22 IOSTANDARD LVCMOS33} [get_ports {PL_LED[0]}]

# SPI 제어 핀 (ili9341)

set_property -dict {PACKAGE_PIN F22 IOSTANDARD LVCMOS33} [get_ports ck_io0_SCK]
set_property -dict {PACKAGE_PIN H20 IOSTANDARD LVCMOS33} [get_ports ck_io1_MOSI]
set_property -dict {PACKAGE_PIN G22 IOSTANDARD LVCMOS33} [get_ports ck_io4_CS]
set_property -dict {PACKAGE_PIN K18 IOSTANDARD LVCMOS33} [get_ports ck_io6_MISO]

# SPI 제어 핀 (ps esp32-s3-wroom-1)

set_property -dict {PACKAGE_PIN F18 IOSTANDARD LVCMOS33} [get_ports esp_SCK_ps]
set_property -dict {PACKAGE_PIN B19 IOSTANDARD LVCMOS33} [get_ports esp_MOSI_ps]
set_property -dict {PACKAGE_PIN A21 IOSTANDARD LVCMOS33} [get_ports esp_MISO_ps]
set_property -dict {PACKAGE_PIN D18 IOSTANDARD LVCMOS33} [get_ports esp_CS_ps]

# UART 제어 핀

set_property -dict {PACKAGE_PIN D20 IOSTANDARD LVCMOS33} [get_ports tx]
set_property -dict {PACKAGE_PIN B21 IOSTANDARD LVCMOS33} [get_ports rx]

# SPI 제어 핀 (배열/Bus 형태)

set_property -dict {PACKAGE_PIN H22 IOSTANDARD LVCMOS33} [get_ports {ck_io2_DC[0]}]
set_property -dict {PACKAGE_PIN H19 IOSTANDARD LVCMOS33} [get_ports {ck_io3_RST[0]}]
set_property -dict {PACKAGE_PIN L21 IOSTANDARD LVCMOS33} [get_ports {ck_io5_LED[0]}]

# 오디오 출력

set_property -dict {PACKAGE_PIN AA13 IOSTANDARD LVCMOS33} [get_ports audio_l]
set_property -dict {PACKAGE_PIN U16 IOSTANDARD LVCMOS33} [get_ports audio_r]

# PT2258 I2C 제어 (이름에 _0 을 하나 더 추가해야 합니다)

set_property -dict {PACKAGE_PIN AA14 IOSTANDARD LVCMOS33} [get_ports IIC_0_0_sda_io]
set_property -dict {PACKAGE_PIN Y16 IOSTANDARD LVCMOS33} [get_ports IIC_0_0_scl_io]
set_property -dict {PACKAGE_PIN W18 IOSTANDARD LVCMOS33} [get_ports CODE1]
set_property -dict {PACKAGE_PIN AA18 IOSTANDARD LVCMOS33} [get_ports CODE2]

#esp32 의 리셋

set_property -dict {PACKAGE_PIN A16 IOSTANDARD LVCMOS33} [get_ports esp_rst]


set_clock_groups -asynchronous -quiet \
    -group [get_clocks -filter {NAME =~ *clk_fpga_0*}] \
    -group [get_clocks -filter {NAME =~ *clk_fpga_1*}] \   
    -group [get_clocks -filter {NAME =~ *clk_fpga_2*}]
    