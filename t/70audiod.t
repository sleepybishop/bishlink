use strict;
use warnings;
use Test::More;
use t::Util;

subtest "audio daemon capture and mapping test" => sub {
	my ($stderr, $stdout) = run_prog("./t/00util/test_audiod");
	like $stdout, qr/===AUDIOD OK===/, "audio daemon captured mock samples and transmitted over UDS successfully";
};

done_testing();
