library ieee;
library work;
use ieee.numeric_std.all;
use ieee.std_logic_1164.all;
use work.fft_types.all;
use work.dcram;
use work.channelSelector;
use work.channelizer1024_16;
use work.axiRamWriter;

entity fmChannelizer is
	generic(inBits, outBits, ramDepthOrder: integer;
			outWidth: integer := 32);
	port(inClk, outClk, outClk_unbuffered: in std_logic;
			din_tdata: in std_logic_vector(inBits*2-1 downto 0);
			din_tvalid: in std_logic := '1';
			din_tready: out std_logic;
			
			dout_tdata: out std_logic_vector(outWidth*2-1 downto 0);
			dout_tvalid: out std_logic;
			dout_tlast: out std_logic;

		-- channels ram access
	
		--axi memory mapped slave, read side
			ctrl_aclk,ctrl_rst: in std_logic;
			ctrl_arready: out std_logic;
			ctrl_arvalid: in std_logic;
			ctrl_araddr: in std_logic_vector(ramDepthOrder-1 downto 0);
			ctrl_arprot: in std_logic_vector(2 downto 0);
			
			ctrl_rvalid: out std_logic;
			ctrl_rready: in std_logic;
			ctrl_rdata: out std_logic_vector(32-1 downto 0);
		
		--axi memory mapped slave, write side
			ctrl_awaddr: in std_logic_vector(ramDepthOrder-1 downto 0);
			ctrl_awprot: in std_logic_vector(2 downto 0);
			ctrl_awvalid: in std_logic;
			ctrl_awready: out std_logic;
			ctrl_wdata: in std_logic_vector(32-1 downto 0);
			ctrl_wvalid: in std_logic;
			ctrl_wready: out std_logic;

			ctrl_bvalid: out std_logic;
			ctrl_bready: in std_logic;
			ctrl_bresp: out std_logic_vector(1 downto 0)
			);
end entity;
architecture a of fmChannelizer is
	constant channelBits: integer := 10;
	signal din, chOut, dout: complex;
	signal chChannel: unsigned(channelBits-1 downto 0);
	signal chValid, chSelValid, chSelLast: std_logic;
	signal tlast0, tlast: std_logic;

	-- 8192 counts * 16 channels * 8 bytes = 1MiB
	signal tlastCounter: unsigned(12 downto 0) := (others=>'0');

	signal ramWClk: std_logic;
	signal ramWEn: std_logic;
	signal ramWAddr: unsigned(ramDepthOrder-1 downto 0);
	signal ramWData: unsigned(channelBits downto 0);
	signal ramWData0: std_logic_vector(31 downto 0);

	attribute X_INTERFACE_INFO : string;
	attribute X_INTERFACE_PARAMETER : string;

	attribute X_INTERFACE_INFO of inClk : signal is "xilinx.com:signal:clock:1.0 inClk CLK";
	attribute X_INTERFACE_PARAMETER of inClk: signal is "ASSOCIATED_BUSIF din";
	attribute X_INTERFACE_INFO of outClk : signal is "xilinx.com:signal:clock:1.0 outClk CLK";
	attribute X_INTERFACE_PARAMETER of outClk: signal is "ASSOCIATED_BUSIF dout";
	attribute X_INTERFACE_INFO of ctrl_aclk : signal is "xilinx.com:signal:clock:1.0 ctrl_aclk CLK";
	attribute X_INTERFACE_PARAMETER of ctrl_aclk: signal is "ASSOCIATED_BUSIF ctrl";

	attribute X_INTERFACE_INFO of din_tvalid: signal is "xilinx.com:interface:axis_rtl:1.0 din tvalid";
	attribute X_INTERFACE_INFO of din_tready: signal is "xilinx.com:interface:axis_rtl:1.0 din tready";
	attribute X_INTERFACE_INFO of din_tdata: signal is "xilinx.com:interface:axis_rtl:1.0 din tdata";
	attribute X_INTERFACE_INFO of dout_tvalid: signal is "xilinx.com:interface:axis_rtl:1.0 dout tvalid";
	attribute X_INTERFACE_INFO of dout_tdata: signal is "xilinx.com:interface:axis_rtl:1.0 dout tdata";
	attribute X_INTERFACE_INFO of dout_tlast: signal is "xilinx.com:interface:axis_rtl:1.0 dout tlast";
begin
	din <= complex_unpack(din_tdata);
	din_tready <= '1';

	ch: entity channelizer1024_16
		generic map(inBits=>inBits, outBits=>outBits)
		port map(inClk=>inClk, outClk=>outClk, outClk_unbuffered=>outClk_unbuffered,
			din=>din, dinValid=>din_tvalid,
			dout=>chOut, doutChannel=>chChannel, doutValid=>chValid);

	chSel: entity channelSelector
		generic map(dataBits=>outBits, channelBits=>channelBits, ramDepthOrder=>ramDepthOrder)
		port map(clk=>outClk, din=>chOut, dinChannel=>chChannel, dinValid=>chValid,
				dout=>dout, doutValid=>chSelValid, doutLast=>chSelLast,
				ramWClk=>ramWClk, ramWEn=>ramWEn, ramWAddr=>ramWAddr, ramWData=>ramWData);
	ramWClk <= ctrl_aclk;

	dout_tvalid <= chSelValid;

	-- assert tlast every time tlastCounter overflows
	tlastCounter <= tlastCounter+1 when chSelLast='1' and chSelValid='1' and rising_edge(outClk);
	tlast0 <= '1' when tlastCounter=(tlastCounter'range=>'1') else '0';
	tlast <= tlast0 when rising_edge(outClk);
	dout_tlast <= chSelLast and tlast;

	axiRam: entity axiRamWriter
		generic map(memAddrWidth=>ramDepthOrder, wordWidth=>32)
		port map(ctrl_aclk,ctrl_rst,ctrl_arready,ctrl_arvalid,ctrl_araddr,ctrl_arprot,
			ctrl_rvalid,ctrl_rready,ctrl_rdata,
			ctrl_awaddr,ctrl_awprot,ctrl_awvalid,ctrl_awready,
			ctrl_wdata,ctrl_wvalid,ctrl_wready,
			ctrl_bvalid,ctrl_bready,ctrl_bresp,
			ramWAddr, ramWData0, ramWEn);
	ramWData <= unsigned(ramWData0(ramWData'range));

	dout_tdata <= complex_pack(dout, outWidth);
end a;
