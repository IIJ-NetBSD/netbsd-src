#! /usr/bin/env perl
# Copyright 2016-2025 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

use strict;
use feature 'state';

use OpenSSL::Test qw/:DEFAULT cmdstr srctop_file bldtop_dir/;
use OpenSSL::Test::Utils;
use TLSProxy::Proxy;
use TLSProxy::Message;

my $test_name = "test_sslrecords";
setup($test_name);

plan skip_all => "TLSProxy isn't usable on $^O"
    if $^O =~ /^(VMS)$/;

plan skip_all => "$test_name needs the dynamic engine feature enabled"
    if disabled("engine") || disabled("dynamic-engine");

plan skip_all => "$test_name needs the sock feature enabled"
    if disabled("sock");

my $inject_recs_num = undef;
my $content_type = undef;
my $boundary_test_type = undef;
my $fatal_alert = undef; # set by filters at expected fatal alerts
my $sslv2testtype = undef;
my $proxy_start_success = 0;

plan tests => 44;

SKIP: {
    skip "TLS 1.2 is disabled", 22 if disabled("tls1_2");
    # Run tests with TLS
    run_tests(0);
}

SKIP: {
    skip "DTLS 1.2 is disabled", 22 if disabled("dtls1_2");
    skip "DTLSProxy does not work on Windows", 22 if $^O =~ /^(MSWin32)$/;
    run_tests(1);
}

sub run_tests
{
    my $run_test_as_dtls = shift;

    my $proxy;
    if ($run_test_as_dtls == 1) {
        $proxy = TLSProxy::Proxy->new_dtls(
            \&add_empty_recs_filter,
            cmdstr(app([ "openssl" ]), display => 1),
            srctop_file("apps", "server.pem"),
            (!$ENV{HARNESS_ACTIVE} || $ENV{HARNESS_VERBOSE})
        );
    } else {
        $proxy = TLSProxy::Proxy->new(
            \&add_empty_recs_filter,
            cmdstr(app([ "openssl" ]), display => 1),
            srctop_file("apps", "server.pem"),
            (!$ENV{HARNESS_ACTIVE} || $ENV{HARNESS_VERBOSE})
        );
    }

    $fatal_alert = 0; # set by filters at expected fatal alerts
    SKIP: {
        skip "Record tests not intended for dtls", 1 if $run_test_as_dtls == 1;
        #Test 1: Injecting out of context empty records should fail
        $proxy->clear();
        $content_type = TLSProxy::Record::RT_APPLICATION_DATA;
        $inject_recs_num = 1;
        $fatal_alert = 0;
        $proxy->serverflags("-tls1_2");
        $proxy->clientflags("-no_tls1_3");
        $proxy_start_success = $proxy->start();
        ok($fatal_alert, "Out of context empty records test");
    }

    skip "TLSProxy did not start correctly", 21 if $proxy_start_success == 0
                                                   && $run_test_as_dtls == 0;

    #Test 2: Injecting in context empty records should succeed
    $proxy->clear();
    $content_type = TLSProxy::Record::RT_HANDSHAKE;
    if ($run_test_as_dtls == 1) {
        $proxy->serverflags("-min_protocol DTLSv1.2 -max_protocol DTLSv1.2");
        $proxy->clientflags("-max_protocol DTLSv1.2");
    } else {
        $proxy->serverflags("-tls1_2");
        $proxy->clientflags("-no_tls1_3");
    }
    $proxy_start_success = $proxy->start();

    skip "TLSProxy did not start correctly", 20 if $proxy_start_success == 0
                                                   && $run_test_as_dtls == 1;

    ok($proxy_start_success && TLSProxy::Message->success(),
       "In context empty records test".($run_test_as_dtls == 1) ? " for DTLS" : " for TLS");

    SKIP: {
        skip "Record tests not intended for dtls", 7 if $run_test_as_dtls == 1;
        #Test 3: Injecting too many in context empty records should fail
        $fatal_alert = 0;
        $proxy->clear();
        #We allow 32 consecutive in context empty records
        $inject_recs_num = 33;
        $proxy->serverflags("-tls1_2");
        $proxy->clientflags("-no_tls1_3");
        $proxy->start();
        ok($fatal_alert, "Too many in context empty records test");

        #Test 4: Injecting a fragmented fatal alert should fail. We expect the server to
        #        send back an alert of its own because it cannot handle fragmented
        #        alerts
        $fatal_alert = 0;
        $proxy->clear();
        $proxy->filter(\&add_frag_alert_filter);
        $proxy->serverflags("-tls1_2");
        $proxy->clientflags("-no_tls1_3");
        $proxy->start();
        ok($fatal_alert, "Fragmented alert records test");

        #Run some SSLv2 ClientHello tests

        use constant {
            TLSV1_2_IN_SSLV2      => 0,
            SSLV2_IN_SSLV2        => 1,
            FRAGMENTED_IN_TLSV1_2 => 2,
            FRAGMENTED_IN_SSLV2   => 3,
            ALERT_BEFORE_SSLV2    => 4
        };

        # The TLSv1.2 in SSLv2 ClientHello need to run at security level 0
        # because in a SSLv2 ClientHello we can't send extensions to indicate
        # which signature algorithm we want to use, and the default is SHA1.

        #Test 5: Inject an SSLv2 style record format for a TLSv1.2 ClientHello
        $sslv2testtype = TLSV1_2_IN_SSLV2;
        $proxy->clear();
        $proxy->filter(\&add_sslv2_filter);
        $proxy->serverflags("-tls1_2");
        $proxy->clientflags("-no_tls1_3 -legacy_renegotiation");
        $proxy->ciphers("AES128-SHA:\@SECLEVEL=0");
        $proxy->start();
        ok(TLSProxy::Message->success(), "TLSv1.2 in SSLv2 ClientHello test");

        #Test 6: Inject an SSLv2 style record format for an SSLv2 ClientHello. We don't
        #        support this so it should fail. We actually treat it as an unknown
        #        protocol so we don't even send an alert in this case.
        $sslv2testtype = SSLV2_IN_SSLV2;
        $proxy->clear();
        $proxy->serverflags("-tls1_2");
        $proxy->clientflags("-no_tls1_3");
        $proxy->ciphers("AES128-SHA:\@SECLEVEL=0");
        $proxy->start();
        ok(TLSProxy::Message->fail(), "SSLv2 in SSLv2 ClientHello test");

        #Test 7: Sanity check ClientHello fragmentation. This isn't really an SSLv2 test
        #        at all, but it gives us confidence that Test 8 fails for the right
        #        reasons
        $sslv2testtype = FRAGMENTED_IN_TLSV1_2;
        $proxy->clear();
        $proxy->serverflags("-tls1_2");
        $proxy->clientflags("-no_tls1_3");
        $proxy->ciphers("AES128-SHA:\@SECLEVEL=0");
        $proxy->start();
        ok(TLSProxy::Message->success(), "Fragmented ClientHello in TLSv1.2 test");

        #Test 8: Fragment a TLSv1.2 ClientHello across a TLS1.2 record; an SSLv2
        #        record; and another TLS1.2 record. This isn't allowed so should fail
        $sslv2testtype = FRAGMENTED_IN_SSLV2;
        $proxy->clear();
        $proxy->serverflags("-tls1_2");
        $proxy->clientflags("-no_tls1_3");
        $proxy->ciphers("AES128-SHA:\@SECLEVEL=0");
        $proxy->start();
        ok(TLSProxy::Message->fail(), "Fragmented ClientHello in TLSv1.2/SSLv2 test");

        #Test 9: Send a TLS warning alert before an SSLv2 ClientHello. This should
        #        fail because an SSLv2 ClientHello must be the first record.
        $sslv2testtype = ALERT_BEFORE_SSLV2;
        $proxy->clear();
        $proxy->serverflags("-tls1_2");
        $proxy->clientflags("-no_tls1_3");
        $proxy->ciphers("AES128-SHA:\@SECLEVEL=0");
        $proxy->start();
        ok(TLSProxy::Message->fail(), "Alert before SSLv2 ClientHello test");
   }
    #Unrecognised record type tests

    #Test 10: Sending an unrecognised record type in TLS1.2 should fail
    $fatal_alert = 0;
    $proxy->clear();
    if ($run_test_as_dtls == 1) {
        $proxy->serverflags("-min_protocol DTLSv1.2 -max_protocol DTLSv1.2");
        $proxy->clientflags("-max_protocol DTLSv1.2");
    } else {
        $proxy->serverflags("-tls1_2");
        $proxy->clientflags("-no_tls1_3");
    }
    $proxy->filter(\&add_unknown_record_type);
    $proxy_start_success = $proxy->start();

    if ($run_test_as_dtls == 1) {
        ok($proxy_start_success == 0, "Unrecognised record type in DTLS1.2");
    } else {
        ok($fatal_alert, "Unrecognised record type in TLS1.2");
    }

    SKIP: {
        skip "TLSv1.1 or DTLSv1 disabled", 1 if ($run_test_as_dtls == 0 && disabled("tls1_1"))
                                                 || ($run_test_as_dtls == 1 && disabled("dtls1"));

        #Test 11: Sending an unrecognised record type in TLS1.1 should fail
        $fatal_alert = 0;
        $proxy->clear();
        if ($run_test_as_dtls == 1) {
            $proxy->clientflags("-min_protocol DTLSv1 -max_protocol DTLSv1 -cipher DEFAULT:\@SECLEVEL=0");
        } else {
            $proxy->clientflags("-tls1_1 -cipher DEFAULT:\@SECLEVEL=0");
        }
        $proxy->ciphers("AES128-SHA:\@SECLEVEL=0");
        $proxy_start_success = $proxy->start();
        if ($run_test_as_dtls == 1) {
            ok($proxy_start_success == 0, "Unrecognised record type in DTLSv1");
        } else {
            ok($fatal_alert, "Unrecognised record type in TLSv1.1");
        }
    }

    SKIP: {
        skip "Record tests not intended for dtls", 10 if $run_test_as_dtls == 1;
        #Test 12: Sending a different record version in TLS1.2 should fail
        $fatal_alert = 0;
        $proxy->clear();
        $proxy->clientflags("-tls1_2");
        $proxy->filter(\&change_version);
        $proxy->start();
        ok($fatal_alert, "Changed record version in TLS1.2");

        #TLS1.3 specific tests
        SKIP: {
            skip "TLSv1.3 disabled", 9
                if disabled("tls1_3") || (disabled("ec") && disabled("dh"));

            #Test 13: Sending a different record version in TLS1.3 should fail
            $proxy->clear();
            $proxy->filter(\&change_version);
            $proxy->start();
            ok(TLSProxy::Message->fail(), "Changed record version in TLS1.3");

            #Test 14: Sending an unrecognised record type in TLS1.3 should fail
            $fatal_alert = 0;
            $proxy->clear();
            $proxy->filter(\&add_unknown_record_type);
            $proxy->start();
            ok($fatal_alert, "Unrecognised record type in TLS1.3");

            #Test 15: Sending an outer record type other than app data once encrypted
            #should fail
            $fatal_alert = 0;
            $proxy->clear();
            $proxy->filter(\&change_outer_record_type);
            $proxy->start();
            ok($fatal_alert, "Wrong outer record type in TLS1.3");

            use constant {
                DATA_AFTER_SERVER_HELLO    => 0,
                DATA_AFTER_FINISHED        => 1,
                DATA_AFTER_KEY_UPDATE      => 2,
                DATA_BETWEEN_KEY_UPDATE    => 3,
                NO_DATA_BETWEEN_KEY_UPDATE => 4,
            };

            #Test 16: Sending a ServerHello which doesn't end on a record boundary
            #         should fail
            $fatal_alert = 0;
            $proxy->clear();
            $boundary_test_type = DATA_AFTER_SERVER_HELLO;
            $proxy->filter(\&not_on_record_boundary);
            $proxy->start();
            ok($fatal_alert, "Record not on boundary in TLS1.3 (ServerHello)");

            #Test 17: Sending a Finished which doesn't end on a record boundary
            #         should fail
            $fatal_alert = 0;
            $proxy->clear();
            $boundary_test_type = DATA_AFTER_FINISHED;
            $proxy->start();
            ok($fatal_alert, "Record not on boundary in TLS1.3 (Finished)");

            #Test 18: Sending a KeyUpdate which doesn't end on a record boundary
            #         should fail
            $fatal_alert = 0;
            $proxy->clear();
            $boundary_test_type = DATA_AFTER_KEY_UPDATE;
            $proxy->start();
            ok($fatal_alert, "Record not on boundary in TLS1.3 (KeyUpdate)");

            #Test 19: Sending application data in the middle of a fragmented KeyUpdate
            #         should fail. Strictly speaking this is not a record boundary test
            #         but we use the same filter.
            $fatal_alert = 0;
            $proxy->clear();
            $boundary_test_type = DATA_BETWEEN_KEY_UPDATE;
            $proxy->start();
            ok($fatal_alert, "Data between KeyUpdate");

            #Test 20: Fragmented KeyUpdate. This should succeed. Strictly speaking this
            #         is not a record boundary test but we use the same filter.
            $proxy->clear();
            $boundary_test_type = NO_DATA_BETWEEN_KEY_UPDATE;
            $proxy->start();
            ok(TLSProxy::Message->success(), "No data between KeyUpdate");

            SKIP: {
                skip "EC disabled", 1 if disabled("ec");

                #Test 21: Force an HRR and change the "real" ServerHello to have a protocol
                #         record version of 0x0301 (TLSv1.0). At this point we have already
                #         decided that we are doing TLSv1.3 but are still using plaintext
                #         records. The server should be sending a record version of 0x303
                #         (TLSv1.2), but the RFC requires us to ignore this field so we
                #         should tolerate the incorrect version.
                $proxy->clear();
                $proxy->filter(\&change_server_hello_version);
                $proxy->serverflags("-groups P-256"); # Force an HRR
                $proxy->start();
                ok(TLSProxy::Message->success(), "Bad ServerHello record version after HRR");
            }
        }
    }

    SKIP: {
        skip "DTLS only record tests", 1 if $run_test_as_dtls != 1;
        #Test 22: We should ignore empty app data records
        $proxy->clear();
        $proxy->filter(\&empty_app_data);
        $proxy->start();
        ok(TLSProxy::Message->success(), "Empty app data in DTLS");

    }
}

sub add_empty_recs_filter
{
    my $proxy = shift;
    my $records = $proxy->record_list;
    my $isdtls = $proxy->isdtls();

    # We're only interested in the initial ClientHello
    if ($proxy->flight != 0) {
        $fatal_alert = 1 if @{$records}[-1]->is_fatal_alert(1) == TLSProxy::Message::AL_DESC_UNEXPECTED_MESSAGE;
        return;
    }

    for (my $i = 0; $i < $inject_recs_num; $i++) {
        my $record;
        if ($isdtls == 1) {
            $record = TLSProxy::Record->new_dtls(
                0,
                $content_type,
                TLSProxy::Record::VERS_DTLS_1_2,
                0,
                0,
                0,
                0,
                0,
                0,
                "",
                ""
            );
        } else {
            $record = TLSProxy::Record->new(
                0,
                $content_type,
                TLSProxy::Record::VERS_TLS_1_2,
                0,
                0,
                0,
                0,
                "",
                ""
            );
        }
        push @{$records}, $record;
    }
}

sub add_frag_alert_filter
{
    my $proxy = shift;
    my $records = $proxy->record_list;
    my $byte;

    # We're only interested in the initial ClientHello
    if ($proxy->flight != 0) {
        $fatal_alert = 1 if @{$records}[-1]->is_fatal_alert(1) == TLSProxy::Message::AL_DESC_UNEXPECTED_MESSAGE;
        return;
    }

    # Add a zero length fragment first
    #my $record = TLSProxy::Record->new(
    #    0,
    #    TLSProxy::Record::RT_ALERT,
    #    TLSProxy::Record::VERS_TLS_1_2,
    #    0,
    #    0,
    #    0,
    #    "",
    #    ""
    #);
    #push @{$proxy->record_list}, $record;

    # Now add the alert level (Fatal) as a separate record
    $byte = pack('C', TLSProxy::Message::AL_LEVEL_FATAL);
    my $record = TLSProxy::Record->new(
        0,
        TLSProxy::Record::RT_ALERT,
        TLSProxy::Record::VERS_TLS_1_2,
        1,
        0,
        1,
        1,
        $byte,
        $byte
    );
    push @{$records}, $record;

    # And finally the description (Unexpected message) in a third record
    $byte = pack('C', TLSProxy::Message::AL_DESC_UNEXPECTED_MESSAGE);
    $record = TLSProxy::Record->new(
        0,
        TLSProxy::Record::RT_ALERT,
        TLSProxy::Record::VERS_TLS_1_2,
        1,
        0,
        1,
        1,
        $byte,
        $byte
    );
    push @{$records}, $record;
}

sub add_sslv2_filter
{
    my $proxy = shift;
    my $clienthello;
    my $record;

    # We're only interested in the initial ClientHello
    if ($proxy->flight != 0) {
        return;
    }

    # Ditch the real ClientHello - we're going to replace it with our own
    shift @{$proxy->record_list};

    if ($sslv2testtype == ALERT_BEFORE_SSLV2) {
        my $alert = pack('CC', TLSProxy::Message::AL_LEVEL_FATAL,
                               TLSProxy::Message::AL_DESC_NO_RENEGOTIATION);
        my $alertlen = length $alert;
        $record = TLSProxy::Record->new(
            0,
            TLSProxy::Record::RT_ALERT,
            TLSProxy::Record::VERS_TLS_1_2,
            $alertlen,
            0,
            $alertlen,
            $alertlen,
            $alert,
            $alert
        );

        push @{$proxy->record_list}, $record;
    }

    if ($sslv2testtype == ALERT_BEFORE_SSLV2
            || $sslv2testtype == TLSV1_2_IN_SSLV2
            || $sslv2testtype == SSLV2_IN_SSLV2) {
        # This is an SSLv2 format ClientHello
        $clienthello =
            pack "C44",
            0x01, # ClientHello
            0x03, 0x03, #TLSv1.2
            0x00, 0x03, # Ciphersuites len
            0x00, 0x00, # Session id len
            0x00, 0x20, # Challenge len
            0x00, 0x00, 0x2f, #AES128-SHA
            0x01, 0x18, 0x9F, 0x76, 0xEC, 0x57, 0xCE, 0xE5, 0xB3, 0xAB, 0x79, 0x90,
            0xAD, 0xAC, 0x6E, 0xD1, 0x58, 0x35, 0x03, 0x97, 0x16, 0x10, 0x82, 0x56,
            0xD8, 0x55, 0xFF, 0xE1, 0x8A, 0xA3, 0x2E, 0xF6; # Challenge

        if ($sslv2testtype == SSLV2_IN_SSLV2) {
            # Set the version to "real" SSLv2
            vec($clienthello, 1, 8) = 0x00;
            vec($clienthello, 2, 8) = 0x02;
        }

        my $chlen = length $clienthello;

        $record = TLSProxy::Record->new(
            0,
            TLSProxy::Record::RT_HANDSHAKE,
            TLSProxy::Record::VERS_TLS_1_2,
            $chlen,
            1, #SSLv2
            $chlen,
            $chlen,
            $clienthello,
            $clienthello
        );

        push @{$proxy->record_list}, $record;
    } else {
        # For this test we're using a real TLS ClientHello
        $clienthello =
            pack "C49",
            0x01, # ClientHello
            0x00, 0x00, 0x2D, # Message length
            0x03, 0x03, # TLSv1.2
            0x01, 0x18, 0x9F, 0x76, 0xEC, 0x57, 0xCE, 0xE5, 0xB3, 0xAB, 0x79, 0x90,
            0xAD, 0xAC, 0x6E, 0xD1, 0x58, 0x35, 0x03, 0x97, 0x16, 0x10, 0x82, 0x56,
            0xD8, 0x55, 0xFF, 0xE1, 0x8A, 0xA3, 0x2E, 0xF6, # Random
            0x00, # Session id len
            0x00, 0x04, # Ciphersuites len
            0x00, 0x2f, # AES128-SHA
            0x00, 0xff, # Empty reneg info SCSV
            0x01, # Compression methods len
            0x00, # Null compression
            0x00, 0x00; # Extensions len

        # Split this into 3: A TLS record; a SSLv2 record and a TLS record.
        # We deliberately split the second record prior to the Challenge/Random
        # and set the first byte of the random to 1. This makes the second SSLv2
        # record look like an SSLv2 ClientHello
        my $frag1 = substr $clienthello, 0, 6;
        my $frag2 = substr $clienthello, 6, 32;
        my $frag3 = substr $clienthello, 38;

        my $fraglen = length $frag1;
        $record = TLSProxy::Record->new(
            0,
            TLSProxy::Record::RT_HANDSHAKE,
            TLSProxy::Record::VERS_TLS_1_2,
            $fraglen,
            0,
            $fraglen,
            $fraglen,
            $frag1,
            $frag1
        );
        push @{$proxy->record_list}, $record;

        $fraglen = length $frag2;
        my $recvers;
        if ($sslv2testtype == FRAGMENTED_IN_SSLV2) {
            $recvers = 1;
        } else {
            $recvers = 0;
        }
        $record = TLSProxy::Record->new(
            0,
            TLSProxy::Record::RT_HANDSHAKE,
            TLSProxy::Record::VERS_TLS_1_2,
            $fraglen,
            $recvers,
            $fraglen,
            $fraglen,
            $frag2,
            $frag2
        );
        push @{$proxy->record_list}, $record;

        $fraglen = length $frag3;
        $record = TLSProxy::Record->new(
            0,
            TLSProxy::Record::RT_HANDSHAKE,
            TLSProxy::Record::VERS_TLS_1_2,
            $fraglen,
            0,
            $fraglen,
            $fraglen,
            $frag3,
            $frag3
        );
        push @{$proxy->record_list}, $record;
    }

}

sub add_unknown_record_type
{
    my $proxy = shift;
    my $records = $proxy->record_list;
    my $isdtls = $proxy->isdtls;
    state $added_record;

    # We'll change a record after the initial version neg has taken place
    if ($proxy->flight == 0) {
        $added_record = 0;
        return;
    } elsif ($proxy->flight != 1 || $added_record) {
        $fatal_alert = 1 if @{$records}[-1]->is_fatal_alert(0) == TLSProxy::Message::AL_DESC_UNEXPECTED_MESSAGE;
        return;
    }

    my $record;

    if ($isdtls) {
        $record = TLSProxy::Record->new_dtls(
            1,
            TLSProxy::Record::RT_UNKNOWN,
            @{$records}[-1]->version(),
            @{$records}[-1]->epoch(),
            @{$records}[-1]->seq() +1,
            1,
            0,
            1,
            1,
            "X",
            "X"
        );
    } else {
        $record = TLSProxy::Record->new(
            1,
            TLSProxy::Record::RT_UNKNOWN,
            @{$records}[-1]->version(),
            1,
            0,
            1,
            1,
            "X",
            "X"
        );
    }

    #Find ServerHello record and insert after that
    my $i;
    for ($i = 0; ${$proxy->record_list}[$i]->flight() < 1; $i++) {
        next;
    }
    $i++;

    splice @{$proxy->record_list}, $i, 0, $record;
    $added_record = 1;
}

sub change_version
{
    my $proxy = shift;
    my $records = $proxy->record_list;

    # We'll change a version after the initial version neg has taken place
    if ($proxy->flight != 1) {
        $fatal_alert = 1 if @{$records}[-1]->is_fatal_alert(0) == TLSProxy::Message::AL_DESC_PROTOCOL_VERSION;
        return;
    }

    if ($#{$records} > 1) {
        # ... typically in ServerHelloDone
        @{$records}[-1]->version(TLSProxy::Record::VERS_TLS_1_1);
    }
}

sub change_server_hello_version
{
    my $proxy = shift;
    my $records = $proxy->record_list;

    # We're only interested in changing the ServerHello after an HRR
    if ($proxy->flight != 3) {
        return;
    }

    # The ServerHello has index 5
    # 0 - ClientHello
    # 1 - HRR
    # 2 - CCS
    # 3 - ClientHello(2)
    # 4 - CCS
    # 5 - ServerHello
    @{$records}[5]->version(TLSProxy::Record::VERS_TLS_1_0);
}

sub change_outer_record_type
{
    my $proxy = shift;
    my $records = $proxy->record_list;

    # We'll change a record after the initial version neg has taken place
    if ($proxy->flight != 1) {
        $fatal_alert = 1 if @{$records}[-1]->is_fatal_alert(0) == TLSProxy::Message::AL_DESC_UNEXPECTED_MESSAGE;
        return;
    }

    # Find CCS record and change record after that
    my $i = 0;
    foreach my $record (@{$records}) {
        last if $record->content_type == TLSProxy::Record::RT_CCS;
        $i++;
    }
    if (defined(${$records}[++$i])) {
        ${$records}[$i]->outer_content_type(TLSProxy::Record::RT_HANDSHAKE);
    }
}

sub not_on_record_boundary
{
    my $proxy = shift;
    my $records = $proxy->record_list;
    my $data;

    #Find server's first flight
    if ($proxy->flight != 1) {
        $fatal_alert = 1 if @{$records}[-1]->is_fatal_alert(0) == TLSProxy::Message::AL_DESC_UNEXPECTED_MESSAGE;
        return;
    }

    if ($boundary_test_type == DATA_AFTER_SERVER_HELLO) {
        #Merge the ServerHello and EncryptedExtensions records into one
        my $i = 0;
        foreach my $record (@{$records}) {
            if ($record->content_type == TLSProxy::Record::RT_HANDSHAKE) {
                $record->{sent} = 1;    # pretend it's sent already
                last;
            }
            $i++;
        }

        if (defined(${$records}[$i+1])) {
            $data = ${$records}[$i]->data();
            $data .= ${$records}[$i+1]->decrypt_data();
            ${$records}[$i+1]->data($data);
            ${$records}[$i+1]->len(length $data);

            #Delete the old ServerHello record
            splice @{$records}, $i, 1;
        }
    } elsif ($boundary_test_type == DATA_AFTER_FINISHED) {
        return if @{$proxy->{message_list}}[-1]->{mt}
                  != TLSProxy::Message::MT_FINISHED;

        my $last_record = @{$records}[-1];
        $data = $last_record->decrypt_data;

        #Add a KeyUpdate message onto the end of the Finished record
        my $keyupdate = pack "C5",
            0x18, # KeyUpdate
            0x00, 0x00, 0x01, # Message length
            0x00; # Update not requested

        $data .= $keyupdate;

        #Add content type and tag
        $data .= pack("C", TLSProxy::Record::RT_HANDSHAKE).("\0"x16);

        #Update the record
        $last_record->data($data);
        $last_record->len(length $data);
    } elsif ($boundary_test_type == DATA_AFTER_KEY_UPDATE) {
        return if @{$proxy->{message_list}}[-1]->{mt}
                  != TLSProxy::Message::MT_FINISHED;

        #KeyUpdates must end on a record boundary

        my $record = TLSProxy::Record->new(
            1,
            TLSProxy::Record::RT_APPLICATION_DATA,
            TLSProxy::Record::VERS_TLS_1_2,
            0,
            0,
            0,
            0,
            "",
            ""
        );

        #Add two KeyUpdate messages into a single record
        my $keyupdate = pack "C5",
            0x18, # KeyUpdate
            0x00, 0x00, 0x01, # Message length
            0x00; # Update not requested

        $data = $keyupdate.$keyupdate;

        #Add content type and tag
        $data .= pack("C", TLSProxy::Record::RT_HANDSHAKE).("\0"x16);

        $record->data($data);
        $record->len(length $data);
        push @{$records}, $record;
    } else {
        return if @{$proxy->{message_list}}[-1]->{mt}
                  != TLSProxy::Message::MT_FINISHED;

        my $record = TLSProxy::Record->new(
            1,
            TLSProxy::Record::RT_APPLICATION_DATA,
            TLSProxy::Record::VERS_TLS_1_2,
            0,
            0,
            0,
            0,
            "",
            ""
        );

        #Add a partial KeyUpdate message into the record
        $data = pack "C1",
            0x18; # KeyUpdate message type. Omit the rest of the message header

        #Add content type and tag
        $data .= pack("C", TLSProxy::Record::RT_HANDSHAKE).("\0"x16);

        $record->data($data);
        $record->len(length $data);
        push @{$records}, $record;

        if ($boundary_test_type == DATA_BETWEEN_KEY_UPDATE) {
            #Now add an app data record
            $record = TLSProxy::Record->new(
                1,
                TLSProxy::Record::RT_APPLICATION_DATA,
                TLSProxy::Record::VERS_TLS_1_2,
                0,
                0,
                0,
                0,
                "",
                ""
            );

            #Add an empty app data record (just content type and tag)
            $data = pack("C", TLSProxy::Record::RT_APPLICATION_DATA).("\0"x16);

            $record->data($data);
            $record->len(length $data);
            push @{$records}, $record;
        }

        #Now add the rest of the KeyUpdate message
        $record = TLSProxy::Record->new(
            1,
            TLSProxy::Record::RT_APPLICATION_DATA,
            TLSProxy::Record::VERS_TLS_1_2,
            0,
            0,
            0,
            0,
            "",
            ""
        );

        #Add the last 4 bytes of the KeyUpdate record
        $data = pack "C4",
            0x00, 0x00, 0x01, # Message length
            0x00; # Update not requested

        #Add content type and tag
        $data .= pack("C", TLSProxy::Record::RT_HANDSHAKE).("\0"x16);

        $record->data($data);
        $record->len(length $data);
        push @{$records}, $record;

    }
}

sub empty_app_data
{
    my $proxy = shift;

    # We're only interested in the client's Certificate..Finished flight
    if ($proxy->flight != 4) {
        return;
    }

    my $data = pack "C52",
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, #IV
        0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
        0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, #One block of empty padded data
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13; #MAC, assume to be 20 bytes

    # Add a zero length app data record at the end
    # This will have the same sequence number as the subsequent app data record
    # that s_client will send - which will cause that second record to be
    # dropped. But that isn't important for this test.
    my $record = TLSProxy::Record->new_dtls(
        4,
        TLSProxy::Record::RT_APPLICATION_DATA,
        TLSProxy::Record::VERS_DTLS_1_2,
        1,
        1,
        length($data),
        0,
        length($data),
        0,
        $data,
        ""
    );
    push @{$proxy->record_list}, $record;
}
