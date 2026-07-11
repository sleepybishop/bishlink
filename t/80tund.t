use strict;
use warnings;
use Test::More;
use t::Util;

subtest "generic data pipe and TUN daemon test" => sub {
	my ($stderr, $stdout) = run_prog("./t/00util/test_tund");
	like $stdout, qr/===TUND OK===/, "bidirectional TUN packets routed and multiplexed successfully over QUIC loopback";
};

done_testing();
