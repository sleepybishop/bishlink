use strict;
use warnings;
use Test::More;
use t::Util;

subtest "multipath transport test" => sub {
	my ($stderr, $stdout) = run_prog("./t/00util/test_multipath");
	diag "STDERR:\n$stderr";
	diag "STDOUT:\n$stdout";
	like $stdout, qr/===MULTIPATH OK===/, "multipath connection, 4 paths validation, data flow, and qlog debug socket successful";
};

done_testing();
