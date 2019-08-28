library ieee;
library work;
use ieee.numeric_std.all;
use ieee.std_logic_1164.all;

-- if enabled, treat each 64-bit input word as two 32-bit complex numbers
-- (16 bit real/imag), and output each complex number separately as a
-- 64 bit complex (32 bit real/imag).
-- enableFlagNum sets which bit of din_tuser controls enable.
entity axiWordSplitter is
	generic(outWidth: integer := 32;
			tuserWidth: integer := 1;
			enableFlagNum: integer := 0);
	port(aclk: in std_logic;
			din_tvalid: in std_logic;
			din_tready: out std_logic;
			din_tdata: in std_logic_vector(outWidth*2-1 downto 0);
			din_tuser: in std_logic_vector(tuserWidth-1 downto 0);
			din_tlast: in std_logic;

			dout_tvalid: out std_logic;
			dout_tready: in std_logic;
			dout_tdata: out std_logic_vector(outWidth*2-1 downto 0);
			dout_tuser: out std_logic_vector(tuserWidth-1 downto 0);
			dout_tlast: out std_logic);
end entity;
architecture a of axiWordSplitter is
	attribute X_INTERFACE_PARAMETER : string;
	attribute X_INTERFACE_PARAMETER of aclk: signal is "ASSOCIATED_BUSIF din:dout";
	attribute X_INTERFACE_INFO : string;
	attribute X_INTERFACE_INFO of din_tvalid: signal is "xilinx.com:interface:axis_rtl:1.0 din tvalid";
	attribute X_INTERFACE_INFO of din_tready: signal is "xilinx.com:interface:axis_rtl:1.0 din tready";
	attribute X_INTERFACE_INFO of din_tdata: signal is "xilinx.com:interface:axis_rtl:1.0 din tdata";
	attribute X_INTERFACE_INFO of din_tuser: signal is "xilinx.com:interface:axis_rtl:1.0 din tuser";
	attribute X_INTERFACE_INFO of din_tlast: signal is "xilinx.com:interface:axis_rtl:1.0 din tlast";
	attribute X_INTERFACE_INFO of dout_tvalid: signal is "xilinx.com:interface:axis_rtl:1.0 dout tvalid";
	attribute X_INTERFACE_INFO of dout_tready: signal is "xilinx.com:interface:axis_rtl:1.0 dout tready";
	attribute X_INTERFACE_INFO of dout_tdata: signal is "xilinx.com:interface:axis_rtl:1.0 dout tdata";
	attribute X_INTERFACE_INFO of dout_tuser: signal is "xilinx.com:interface:axis_rtl:1.0 dout tuser";
	attribute X_INTERFACE_INFO of dout_tlast: signal is "xilinx.com:interface:axis_rtl:1.0 dout tlast";

	signal din_tdata0: signed(outWidth*2-1 downto 0);
	signal dinA0, dinB0: signed(outWidth-1 downto 0);
	signal dinA, dinB: signed(outWidth*2-1 downto 0);

	signal reg_tvalid, reg_tlast: std_logic;
	signal reg_tdataA, reg_tdataB, reg_tdataC: signed(outWidth*2-1 downto 0);
	signal reg_ce: std_logic;

	signal muxOut: signed(outWidth*2-1 downto 0);

	signal out_advance, counter, intern_tready: std_logic;
	signal enable: std_logic;
begin
	din_tdata0 <= signed(din_tdata);
	dinA0 <= din_tdata0(dinA0'range);
	dinB0 <= din_tdata0(din_tdata0'left downto dinA0'length);

	dinA <= resize(dinA0(outWidth-1 downto outWidth/2), outWidth) &
			resize(dinA0(outWidth/2-1 downto 0), outWidth);
	dinB <= resize(dinB0(outWidth-1 downto outWidth/2), outWidth) &
			resize(dinB0(outWidth/2-1 downto 0), outWidth);

	-- input axi data register
	reg_tvalid <= din_tvalid when reg_ce='1' and rising_edge(aclk);
	reg_tdataA <= dinA when reg_ce='1' and rising_edge(aclk);
	reg_tdataB <= dinB when reg_ce='1' and rising_edge(aclk);
	reg_tdataC <= din_tdata0 when reg_ce='1' and rising_edge(aclk);
	reg_tlast <= din_tlast when reg_ce='1' and rising_edge(aclk);
	enable <= din_tuser(enableFlagNum) when reg_ce='1' and rising_edge(aclk);

	dout_tvalid <= reg_tvalid;

	-- mux
	muxOut <= reg_tdataC when enable='0' else
			reg_tdataA when counter='0' else
			reg_tdataB;
	dout_tdata <= std_logic_vector(muxOut);

	out_advance <= reg_tvalid and dout_tready;
	counter <= not counter when out_advance='1' and rising_edge(aclk);
	intern_tready <= dout_tready when enable='0' else
						counter and out_advance;

	reg_ce <= intern_tready or (not reg_tvalid);
	din_tready <= reg_ce;

	dout_tuser <= din_tuser when reg_ce='1' and rising_edge(aclk);
	dout_tlast <= reg_tlast and counter;
end a;
