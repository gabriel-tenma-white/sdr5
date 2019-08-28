library ieee;
library work;
use ieee.numeric_std.all;
use ieee.std_logic_1164.all;
use work.axiMipmap_types.all;
use work.axiMipmap_generator;
use work.dcfifo2;

entity sdr5_mipmap is
	port(
			din_clk, reset: in std_logic;
			-- complex number, two 32 bit signed values
			din_tdata: in std_logic_vector(63 downto 0);
			din_tvalid: in std_logic;
			din_tready: out std_logic;
			din_tlast: in std_logic := '0';

			dout_clk: in std_logic;
			dout_tdata: out std_logic_vector(63 downto 0);
			dout_tvalid: out std_logic;
			dout_tready: in std_logic;
			dout_tlast: out std_logic);
end entity;
architecture a of sdr5_mipmap is
	attribute X_INTERFACE_INFO : string;
	attribute X_INTERFACE_PARAMETER : string;

	attribute X_INTERFACE_INFO of din_clk : signal is "xilinx.com:signal:clock:1.0 din_clk CLK";
	attribute X_INTERFACE_PARAMETER of din_clk: signal is "ASSOCIATED_BUSIF din";
 	attribute X_INTERFACE_INFO of dout_clk : signal is "xilinx.com:signal:clock:1.0 dout_clk CLK";
	attribute X_INTERFACE_PARAMETER of dout_clk: signal is "ASSOCIATED_BUSIF dout";

	attribute X_INTERFACE_INFO of din_tvalid: signal is "xilinx.com:interface:axis_rtl:1.0 din tvalid";
	attribute X_INTERFACE_INFO of din_tready: signal is "xilinx.com:interface:axis_rtl:1.0 din tready";
	attribute X_INTERFACE_INFO of din_tdata: signal is "xilinx.com:interface:axis_rtl:1.0 din tdata";
	attribute X_INTERFACE_INFO of din_tlast: signal is "xilinx.com:interface:axis_rtl:1.0 din tlast";
	attribute X_INTERFACE_INFO of dout_tvalid: signal is "xilinx.com:interface:axis_rtl:1.0 dout tvalid";
	attribute X_INTERFACE_INFO of dout_tready: signal is "xilinx.com:interface:axis_rtl:1.0 dout tready";
	attribute X_INTERFACE_INFO of dout_tdata: signal is "xilinx.com:interface:axis_rtl:1.0 dout tdata";
	attribute X_INTERFACE_INFO of dout_tlast: signal is "xilinx.com:interface:axis_rtl:1.0 dout tlast";

	constant fifoDepthIn: integer := 9;
	
	signal i_tstrobe, i_tready, i_tlast, i_tready1: std_logic;
	signal i_tdata: std_logic_vector(63 downto 0);
	signal dinA, dinB: signed(31 downto 0);
	signal mipmapIn: minMaxArray(1 downto 0);
	signal mipmapOut: minMaxArray(1 downto 0);
	signal mipmapOutStrobe, mipmapOutReady, mipmapOutLast: std_logic;

	signal fifoIn: std_logic_vector(129 downto 0);
	signal fifoOut: std_logic_vector(64 downto 0);
	signal fifoWrroom: unsigned(fifoDepthIn-1 downto 0);
	signal fifoReady, fifoReady0: std_logic;
begin
	-- convert axi to oxi
	i_tdata <= din_tdata when rising_edge(din_clk);
	i_tlast <= din_tlast when rising_edge(din_clk);
	i_tstrobe <= din_tvalid and i_tready1 when rising_edge(din_clk);
	i_tready1 <= i_tready when rising_edge(din_clk);
	din_tready <= i_tready1;

	i_tready <= mipmapOutReady;

	dinA <= signed(i_tdata(31 downto 0));
	dinB <= signed(i_tdata(63 downto 32));
	mipmapIn <= (to_minMax(dinB, dinB), to_minMax(dinA, dinA));

	mipmapGen: entity axiMipmap_generator
		generic map(channels=>2)
		port map(aclk=>din_clk, reset=>reset,
			in_tdata=>mipmapIn, in_tstrobe=>i_tstrobe, in_tlast=>i_tlast,
			out_tdata=>mipmapOut, out_tstrobe=>mipmapOutStrobe, out_tready=>mipmapOutReady, out_tlast=>mipmapOutLast);

	fifoIn <= mipmapOutLast &
				std_logic_vector(mipmapOut(1).upper) &
				std_logic_vector(mipmapOut(1).lower) &
				'0' &
				std_logic_vector(mipmapOut(0).upper) &
				std_logic_vector(mipmapOut(0).lower);

	outFifo: entity dcfifo2
		generic map(widthIn=>130, widthOut=>65, depthOrderIn=>fifoDepthIn, outputRegisters=>2)
		port map(rdclk=>dout_clk, wrclk=>din_clk,
				rdvalid=>dout_tvalid, rdready=>dout_tready, rddata=>fifoOut,
				wrvalid=>mipmapOutStrobe, wrready=>open, wrdata=>fifoIn,
				wrroom=>fifoWrroom);

	dout_tdata <= fifoOut(63 downto 0);
	dout_tlast <= fifoOut(64);

	fifoReady0 <= '1' when fifoWrroom >= 16 else '0';
	fifoReady <= fifoReady0 when rising_edge(din_clk);
	mipmapOutReady <= fifoReady when rising_edge(din_clk);
end a;

