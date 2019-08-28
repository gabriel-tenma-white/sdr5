library ieee;
library work;
use ieee.numeric_std.all;
use ieee.std_logic_1164.all;
use work.axiPipe_types.all;

-- assuming the address of an entry in a matrix is of the form:
-- [MAddr][RowAddr][ColAddr][burstAddr],
-- interleave the row and column addresses.
entity myAddrPerm is
	port(readAddrIn: in unsigned(memAddrWidth-1 downto 0);
		readAddrFlags: in std_logic_vector(flagsWidth-1 downto 0);
		readAddrOut: out unsigned(memAddrWidth-1 downto 0));
end entity;
architecture a of myAddrPerm is
	constant burstBits: integer := 5;
	signal a00, a01, a10, a11: memAddr_t;
	signal ctrl: std_logic_vector(1 downto 0);

	function myInterleave(a: memAddr_t) return memAddr_t is
		variable tmp,res: memAddr_t;
	begin
		tmp := interleaveAddress(a, burstBits, 9, 9);
--		res(13 downto 0) := tmp(13 downto 0);
--		res(14) := tmp(24);
--		res(31 downto 15) := tmp(31 downto 25) & tmp(23 downto 14);
		return tmp;
	end function;
begin
	-- full width input
	a00 <= myInterleave(readAddrIn);
	a01 <= myInterleave(
				transposeAddress(readAddrIn, burstBits, 9, 9));
	-- half width input
	a10 <= interleaveAddress(readAddrIn, burstBits, 9, 8);
	a11 <= interleaveAddress(
				transposeAddress(readAddrIn, burstBits, 9, 8),
				burstBits, 9, 8);
	ctrl <= readAddrFlags(ctrl'range);
	readAddrOut <=
			a00 when ctrl="00" else
			a01 when ctrl="01" else
			a10 when ctrl="10" else
			a11;
end a;
