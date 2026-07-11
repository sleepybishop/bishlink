use strict;
use warnings;
use Test::More;
use t::Util;

subtest "audio stream transmission test" => sub {
	my ($stderr, $stdout) = run_prog("./t/00util/test_audio");
	like $stdout, qr/===AUDIO OK===/, "wav file successfully streamed over quic and checksum verified";
};

done_testing();
