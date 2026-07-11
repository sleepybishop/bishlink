use strict;
use warnings;
use Test::More;
use t::Util;

subtest "uds ipc integration test" => sub {
	my ($stderr, $stdout) = run_prog("./t/00util/test_uds");
	like $stdout, qr/===UDS OK===/, "video capture and input injection uds interfaces validated successfully";
};

done_testing();
