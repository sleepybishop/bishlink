use strict;
use warnings;
use Test::More;
use t::Util;

subtest "input daemon injection and mapping test" => sub {
	my ($stderr, $stdout) = run_prog("./t/00util/test_inputd");
	like $stdout, qr/===INPUTD OK===/, "input daemon mapped key, relative motion, and syn events successfully";
};

done_testing();
