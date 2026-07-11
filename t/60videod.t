use strict;
use warnings;
use Test::More;
use t::Util;

subtest "video daemon capture and mapping test" => sub {
	my ($stderr, $stdout) = run_prog("./t/00util/test_videod");
	like $stdout, qr/===VIDEOD OK===/, "video daemon captured mock frames and transmitted over UDS successfully";
};

done_testing();
