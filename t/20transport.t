use strict;
use warnings;
use Test::More;
use t::Util;

subtest "transport client-server test" => sub {
	my ($stderr, $stdout) = run_prog("./t/00util/test_transport");
	like $stdout, qr/===TRANSPORT OK===/, "transport connection, subscription, and data flow successful";
};

done_testing();
