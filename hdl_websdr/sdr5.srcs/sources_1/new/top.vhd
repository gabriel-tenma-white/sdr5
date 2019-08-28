

library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

library UNISIM;
use UNISIM.VComponents.all;

library work;
use work.design_1_wrapper;
use work.slow_clock;
use work.clockDataGating;
use work.sr;
use work.fft_types.all;
use work.transposer4;
use work.sr_complex;

entity top is
	Port ( CLOCK_26_IN : in STD_LOGIC;
		   --GPIO_B : inout std_logic_vector(3 downto 0);
		   AD9361_TXD_P, AD9361_TXD_N: out signed(5 downto 0);
		   AD9361_RXD_P, AD9361_RXD_N: in signed(5 downto 0);
		   
		   AD9361_FB_CLK_P,AD9361_FB_CLK_N: out std_logic;
		   AD9361_TX_FRM_P,AD9361_TX_FRM_N: out std_logic;
		   AD9361_DATA_CLK_P,AD9361_DATA_CLK_N: in std_logic;
		   AD9361_RX_FRM_P,AD9361_RX_FRM_N: in std_logic;
		   
		   AD9361_ENABLE, AD9361_TXNRX, AD9361_RESET: out std_logic;
		   AD9361_SPI_CLK, AD9361_SPI_CS, AD9361_SPI_SDI: out std_logic;
           AD9361_SPI_SDO: in std_logic;
		   LED_R,LED_B: out std_logic;
		   
		   -- HPS i/o
		   DDR_addr : inout STD_LOGIC_VECTOR ( 14 downto 0 );
		   DDR_ba : inout STD_LOGIC_VECTOR ( 2 downto 0 );
		   DDR_cas_n : inout STD_LOGIC;
		   DDR_ck_n : inout STD_LOGIC;
		   DDR_ck_p : inout STD_LOGIC;
		   DDR_cke : inout STD_LOGIC;
		   DDR_cs_n : inout STD_LOGIC;
		   DDR_dm : inout STD_LOGIC_VECTOR ( 3 downto 0 );
		   DDR_dq : inout STD_LOGIC_VECTOR ( 31 downto 0 );
		   DDR_dqs_n : inout STD_LOGIC_VECTOR ( 3 downto 0 );
		   DDR_dqs_p : inout STD_LOGIC_VECTOR ( 3 downto 0 );
		   DDR_odt : inout STD_LOGIC;
		   DDR_ras_n : inout STD_LOGIC;
		   DDR_reset_n : inout STD_LOGIC;
		   DDR_we_n : inout STD_LOGIC;
		   FIXED_IO_ddr_vrn : inout STD_LOGIC;
		   FIXED_IO_ddr_vrp : inout STD_LOGIC;
		   FIXED_IO_mio : inout STD_LOGIC_VECTOR ( 53 downto 0 );
		   FIXED_IO_ps_clk : inout STD_LOGIC;
		   FIXED_IO_ps_porb : inout STD_LOGIC;
		   FIXED_IO_ps_srstb : inout STD_LOGIC
		   );
end top;

architecture a of top is
	component clk_wiz_0 port (
	  clk_out1          : out    std_logic;
	  clk_out2          : out    std_logic;
	  clk_in1           : in     std_logic);
	end component;
	
	signal internalclk,H2FCLK0,ad_clk: std_logic;
	signal CLOCK_330, CLOCK_200: std_logic;
	--signal fftClk,fftClk_unbuffered: std_logic;
	signal axiPipeClk: std_logic;
	signal channelizerClk, channelizerClk_unbuffered: std_logic;
	signal channelizerIn_tdata : STD_LOGIC_VECTOR ( 23 downto 0 );
	
	
	signal cnt: unsigned(3 downto 0);
	signal AD9361_TXD, AD9361_RXD, AD9361_TXD_INV, AD9361_RXD_INV: signed(5 downto 0);
	signal AD9361_DATA_CLK, AD9361_FB_CLK, AD9361_DATA_CLK_INV: std_logic;
	signal AD9361_TX_FRM, AD9361_RX_FRM: std_logic;
	
	-- after iddr
	signal ad_rxdI,ad_rxdQ,ad_rxdI1,ad_rxdQ1,ad_rxdI2,ad_rxdQ2: signed(5 downto 0);
	signal ad_rxFI,ad_rxFQ,ad_rxFI1,ad_rxFQ1,ad_rxFI2,ad_rxFQ2: std_logic;
	-- after framing
	signal ad_rxI0, ad_rxQ0, ad_rxI, ad_rxQ: signed(11 downto 0);
	
	signal EMIO_GPIO_I,EMIO_GPIO_O,EMIO_GPIO_T: STD_LOGIC_VECTOR(31 downto 0);
	signal streamIn_tdata: std_logic_vector(31 downto 0) := (others=>'0');
	signal streamIn_tstrobe,streamIn_tready,streamClk: std_logic;
begin
	
	STARTUPE2_inst : STARTUPE2
			generic map (
			PROG_USR => "FALSE", -- Activate program event security feature. Requires encrypted bitstreams.
			SIM_CCLK_FREQ => 0.0 -- Set the Configuration Clock Frequency(ns) for simulation.
			)
			port map (
				CFGCLK => internalclk,
				-- 1-bit output: Configuration main clock output
				CFGMCLK => open,
				-- 1-bit output: Configuration internal oscillator clock output
				EOS => open,
				-- 1-bit output: Active high output signal indicating the End Of Startup.
				PREQ => open,
				-- 1-bit output: PROGRAM request to fabric output
				CLK => '0',
				-- 1-bit input: User start-up clock input
				GSR => '0',
				-- 1-bit input: Global Set/Reset input (GSR cannot be used for the port name)
				GTS => '0',
				-- 1-bit input: Global 3-state input (GTS cannot be used for the port name)
				KEYCLEARB => '0', -- 1-bit input: Clear AES Decrypter Key input from Battery-Backed RAM (BBRAM)
				PACK => '0',
				-- 1-bit input: PROGRAM acknowledge input
				USRCCLKO => '0',
				-- 1-bit input: User CCLK input
				USRCCLKTS => '0', -- 1-bit input: User CCLK 3-state enable input
				USRDONEO => '0',
				-- 1-bit input: User DONE pin output control
				USRDONETS => '0' -- 1-bit input: User DONE 3-state enable output
			);
	
	clkgen : clk_wiz_0 port map ( 
		-- Clock out ports  
		clk_out1 => CLOCK_330,
		clk_out2 => CLOCK_200,
		-- Clock in ports
		clk_in1 => CLOCK_26_IN
		);
	--fftClk_unbuffered <= CLOCK_230;
	channelizerClk_unbuffered <= CLOCK_330;
	axiPipeClk <= CLOCK_200;
	
	--TMPCLK <= CLOCK_19_2_IN;
	
	--iobuf1: IOBUF port map(T=>'0', I=>CLOCK_19_2_IN, O=>TMPCLK1, IO=>TMPCLK);
	--ibufclk: IBUFGDS port map(I=>CLOCK_19_2_IN_P, IB=>CLOCK_19_2_IN_N, O=>CLOCK_19_2);
	--bufg1: BUFG port map(I=>TMPCLK1, O=>CLOCK_19_2);
	
	--obuf1: OBUF port map(I=>CLOCK_19_2_IN, O=>TMPCLK);
	--bufg1: BUFG port map(I=>TMPCLK, O=>CLOCK_19_2);
	
	--cnt <= cnt+1+unsigned(GPIO_B) when rising_edge(H2FCLK0);
	--GPIO_B(2 downto 0) <= std_logic_vector(cnt(2 downto 0));
	--GPIO_B(0) <= not GPIO_B(0) when rising_edge(H2FCLK0);
	
	--AD9361_TXD <= signed(resize(cnt, 5)) & AD9361_RXD(0);
	
	ibuf1_data_clk: IBUFDS port map(I=>AD9361_DATA_CLK_P, IB=>AD9361_DATA_CLK_N, O=>AD9361_DATA_CLK_INV);
	ibuf1_rx_frm: IBUFDS port map(I=>AD9361_RX_FRM_P, IB=>AD9361_RX_FRM_N, O=>AD9361_RX_FRM);
	obuf1_fb_clk: OBUFDS port map(I=>AD9361_FB_CLK, O=>AD9361_FB_CLK_P, OB=>AD9361_FB_CLK_N);
	obuf1_tx_frm: OBUFDS port map(I=>AD9361_TX_FRM, O=>AD9361_TX_FRM_P, OB=>AD9361_TX_FRM_N);
	gen_diffbuff: for X in 0 to 5 generate
		obuf1: OBUFDS port map(I=>AD9361_TXD_INV(X), O=>AD9361_TXD_P(X), OB=>AD9361_TXD_N(X));
		ibuf1: IBUFDS port map(I=>AD9361_RXD_P(X), IB=>AD9361_RXD_N(X), O=>AD9361_RXD_INV(X));
		
		-- ad9361 ddr data is falling-edge-first, meaning SAME_EDGE mode will give us the right
		-- alignment and ordering
		iddr1: IDDR generic map(DDR_CLK_EDGE=>"SAME_EDGE")
				port map(CE=>'1', C=>AD9361_DATA_CLK, D=>AD9361_RXD(X), Q2=>ad_rxdI(X), Q1=>ad_rxdQ(X));
	end generate;
	
	-- correct for inversions in layout design
	AD9361_DATA_CLK <= not AD9361_DATA_CLK_INV;
	AD9361_RXD(0) <= AD9361_RXD_INV(0);
	AD9361_RXD(1) <= AD9361_RXD_INV(1);
	AD9361_RXD(2) <= AD9361_RXD_INV(2);
	AD9361_RXD(3) <= AD9361_RXD_INV(3);
	AD9361_RXD(4) <= not AD9361_RXD_INV(4);
	AD9361_RXD(5) <= not AD9361_RXD_INV(5);
	AD9361_TXD_INV(0) <= AD9361_TXD(0);
	AD9361_TXD_INV(1) <= not AD9361_TXD(1);
	AD9361_TXD_INV(2) <= not AD9361_TXD(2);
	AD9361_TXD_INV(3) <= not AD9361_TXD(3);
	AD9361_TXD_INV(4) <= not AD9361_TXD(4);
	AD9361_TXD_INV(5) <= AD9361_TXD(5);
	
	
	iddr2: IDDR generic map(DDR_CLK_EDGE=>"SAME_EDGE")
			port map(CE=>'1', C=>AD9361_DATA_CLK, D=>AD9361_RX_FRM, Q2=>ad_rxFI, Q1=>ad_rxFQ);
	
	ad_rxdI1 <= ad_rxdI when rising_edge(AD9361_DATA_CLK);
	ad_rxdQ1 <= ad_rxdQ when rising_edge(AD9361_DATA_CLK);
	ad_rxdI2 <= ad_rxdI1 when rising_edge(AD9361_DATA_CLK);
	ad_rxdQ2 <= ad_rxdQ1 when rising_edge(AD9361_DATA_CLK);
	ad_rxFI1 <= ad_rxFI when rising_edge(AD9361_DATA_CLK);
	ad_rxFQ1 <= ad_rxFQ when rising_edge(AD9361_DATA_CLK);
	ad_rxFI2 <= ad_rxFI1 when rising_edge(AD9361_DATA_CLK);
	ad_rxFQ2 <= ad_rxFQ1 when rising_edge(AD9361_DATA_CLK);
	
	
	ad_clk <= AD9361_DATA_CLK;
	
	-- when ad_rxFI/ad_rxFQ is high, ad_rxdI/ad_rxdQ correspond to upper 6 bits, and lower 6 bits otherwise
	ad_rxI0(11 downto 6) <= ad_rxdI2 when ad_rxFI2='1' and rising_edge(ad_clk);
	ad_rxI0(5 downto 0) <= ad_rxdI2 when ad_rxFI2='0' and rising_edge(ad_clk);
	ad_rxQ0(11 downto 6) <= ad_rxdQ2 when ad_rxFI2='1' and rising_edge(ad_clk);
	ad_rxQ0(5 downto 0) <= ad_rxdQ2 when ad_rxFI2='0' and rising_edge(ad_clk);
	ad_rxI <= ad_rxI0 when ad_rxFI2='1' and rising_edge(ad_clk);
	ad_rxQ <= ad_rxQ0 when ad_rxFI2='1' and rising_edge(ad_clk);
	
	-- gpios
	AD9361_SPI_CLK <= EMIO_GPIO_O(0);
	AD9361_SPI_CS <= EMIO_GPIO_O(1);
	AD9361_SPI_SDI <= EMIO_GPIO_O(2);
	AD9361_RESET <= EMIO_GPIO_O(4);
	LED_R <= EMIO_GPIO_O(0);

	EMIO_GPIO_I(31 downto 4) <= EMIO_GPIO_O(31 downto 4);
	EMIO_GPIO_I(3) <= AD9361_SPI_SDO;
	EMIO_GPIO_I(2 downto 0) <= EMIO_GPIO_O(2 downto 0);

	--LED_B <= EMIO_GPIO_O(1);
	
	sc1: entity work.slow_clock
			generic map(divide=>5000000,dutycycle=>2500000)
			port map(clk=>AD9361_DATA_CLK, o=>LED_B);
	
	
	-- ad9361 data
	AD9361_FB_CLK <= AD9361_DATA_CLK;
	AD9361_TX_FRM <= not AD9361_TX_FRM when rising_edge(AD9361_FB_CLK);
	AD9361_TXD <= signed(EMIO_GPIO_O(21 downto 16));
	AD9361_ENABLE <= '1';
	
	streamClk <= ad_clk;
	streamIn_tdata(15 downto 0) <= std_logic_vector(resize(ad_rxI, 16));
	streamIn_tdata(31 downto 16) <= std_logic_vector(resize(ad_rxQ, 16));
	streamIn_tstrobe <= '1' when ad_rxFI='0' else '0';
	
	channelizerIn_tdata <= std_logic_vector(ad_rxQ)
							& std_logic_vector(ad_rxI);
	
	--fftClkBuf: BUFG port map(I=>fftClk_unbuffered, O=>fftClk);
	chClkBuf: BUFG port map(I=>channelizerClk_unbuffered, O=>channelizerClk);

	
	hps: entity design_1_wrapper port map(
		DDR_addr=>DDR_addr,DDR_ba=>DDR_ba,DDR_cas_n=>DDR_cas_n,
		DDR_ck_n=>DDR_ck_n,DDR_ck_p=>DDR_ck_p,DDR_cke=>DDR_cke,
		DDR_cs_n=>DDR_cs_n,DDR_dm=>DDR_dm,DDR_dq=>DDR_dq,
		DDR_dqs_n=>DDR_dqs_n,DDR_dqs_p=>DDR_dqs_p,DDR_odt=>DDR_odt,
		DDR_ras_n=>DDR_ras_n,DDR_reset_n=>DDR_reset_n,DDR_we_n=>DDR_we_n,
		FIXED_IO_ddr_vrn=>FIXED_IO_ddr_vrn,FIXED_IO_ddr_vrp=>FIXED_IO_ddr_vrp,
		FIXED_IO_mio=>FIXED_IO_mio,FIXED_IO_ps_clk=>FIXED_IO_ps_clk,
		FIXED_IO_ps_porb=>FIXED_IO_ps_porb,FIXED_IO_ps_srstb=>FIXED_IO_ps_srstb,
		H2FCLK0=>H2FCLK0,
		EMIO_GPIO_I=>EMIO_GPIO_I,
		EMIO_GPIO_O=>EMIO_GPIO_O,
		EMIO_GPIO_T=>EMIO_GPIO_T,
		streamClk=>streamClk,
		streamIn_tdata=>streamIn_tdata,
		streamIn_tready=>streamIn_tready,
		streamIn_tvalid=>streamIn_tstrobe,
		
		--fftClk=>fftClk,
		--fftClk_unbuffered=>fftClk_unbuffered,
		axiPipeClk=>axiPipeClk,
		channelizerClk=>channelizerClk,
		channelizerClk_unbuffered=>channelizerClk_unbuffered,
		channelizerIn_tdata=>channelizerIn_tdata,
		channelizerIn_tvalid=>streamIn_tstrobe
--		bp_ce=>bp_ce, bp_ostrobe=>bp_ostrobe, bp_inFlags=>bp_inFlags,
--		bp_indata=>bp_indata, bp_inphase=>bp_inphase, bp_outFlags=>bp_outFlags,
--		bp_outdata=>bp_outdata
		);
	
end a;
