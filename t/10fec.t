use strict;
use warnings;
use Test::More;
use t::Util;

subtest "fec recovery test" => sub {
	my ($stderr, $stdout) = run_prog("./t/00util/test_fec");
	like $stdout, qr/===FEC OK===/, "fec recovered data successfully";
};

done_testing();
