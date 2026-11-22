# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_profiles import clipboard as broker
from karyo_profiles import clipboard_client as client
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.storage import ConfigStore

TEXT='text/plain;charset=utf-8\0Hello\nПривет'.encode('utf-8')
PNG=b'image/png\0'+bytes([137,80,78,71,13,10,26,10])+bytes(range(256))


class Families:
    def __init__(self):self.epoch=1;self.groups={1001:('home','work'),1002:('home','work'),1003:('other','child')}
    def resolve(self,uid):return self.epoch,self.groups.get(uid)


class ClipboardPolicyTest(unittest.TestCase):
    def setUp(self):self.families=Families();self.cache=broker.Clipboards(self.families)
    def exchange(self,uid,op,value=b'',known=broker.ZERO):
        return broker.parse_response(self.cache.exchange(uid,broker.request(ord(op),known,value)))

    def test_binary_png_and_utf8_are_exact_and_other_families_cannot_fetch_them(self):
        for value in (TEXT,PNG):
            self.exchange(1001,'U',value)
            kind,revision,actual=self.exchange(1002,'G')
            self.assertEqual((kind,actual),(b'D',value))
            self.assertEqual(revision,hashlib.sha256(b'D'+value).digest())
            self.assertEqual(self.exchange(1003,'G')[0],b'Z')
            self.assertEqual(self.exchange(1000,'G')[0],b'E')
            self.assertEqual(self.exchange(0,'G')[0],b'E')

    def test_restart_seed_never_overwrites_the_latest_shared_selection(self):
        self.exchange(1001,'U',PNG)
        self.assertEqual(self.exchange(1002,'S',TEXT)[2],PNG)
        self.assertEqual(self.exchange(1002,'U',TEXT)[2],TEXT)

    def test_revision_suppresses_echo_but_empty_text_is_not_an_absent_selection(self):
        empty=b'text/plain;charset=utf-8\0'
        unused,revision,unused=self.exchange(1001,'U',empty)
        self.assertEqual(self.exchange(1002,'G',known=revision),(b'N',revision,b''))
        self.assertEqual(self.exchange(1002,'G')[2],empty)
        kind,cleared,value=self.exchange(1001,'C')
        self.assertEqual((kind,value),(b'C',b''));self.assertNotEqual(cleared,broker.ZERO)
        self.assertEqual(self.exchange(1002,'G')[0],b'C')

    def test_membership_and_account_epoch_changes_forget_old_shared_bytes(self):
        self.exchange(1001,'U',TEXT)
        self.families.groups[1002]=None
        self.assertEqual(self.exchange(1002,'G')[0],b'E')
        self.families.epoch+=1
        self.families.groups[1002]=('home','new-work')
        self.assertEqual(self.exchange(1002,'G')[0],b'Z')
        self.assertEqual(len(self.cache.values),0)

    def test_invalid_metadata_clears_volatile_cache_without_logging_payload(self):
        self.exchange(1001,'U',TEXT)
        with patch.object(self.families,'resolve',side_effect=ValueError('unsafe')):
            self.assertEqual(self.exchange(1002,'G')[0],b'E')
        self.assertFalse(self.cache.values)

    def test_bad_requests_and_unsupported_or_oversized_mime_preserve_known_content(self):
        self.exchange(1001,'U',TEXT)
        for value in (b'bad',b'\1X'+broker.ZERO,b'\1G'+broker.ZERO+b'UID=1001',
                      b'\1U'+broker.ZERO+b'text/html\0secret',b'\1U'+broker.ZERO+b'image/png\0'+bytes(broker.LIMIT+1)):
            with self.subTest(length=len(value)),self.assertRaises(ValueError):self.cache.exchange(1002,value)
        self.assertEqual(self.exchange(1002,'G')[2],TEXT)

    def test_aggregate_cache_and_family_count_evict_only_the_oldest(self):
        with patch.object(broker,'CACHE_LIMIT',len(TEXT)+len(PNG)-1):
            self.exchange(1001,'U',TEXT);self.exchange(1003,'U',PNG)
            self.assertEqual(self.exchange(1001,'G')[0],b'Z')
            self.assertEqual(self.exchange(1003,'G')[2],PNG)
        with patch.object(broker,'CACHE_FAMILIES',1):
            self.exchange(1001,'U',TEXT)
            self.assertEqual(self.exchange(1003,'G')[0],b'Z')

    def test_frame_reader_rejects_truncated_and_overflow_headers(self):
        for content in (bytes([0]),struct.pack('!I',broker.FRAME_LIMIT+1),struct.pack('!I',34)+b'short'):
            a,b=socket.socketpair()
            with a,b:
                a.sendall(content);a.shutdown(socket.SHUT_WR)
                with self.assertRaises(ValueError):broker.receive(b)

    def test_real_socket_peer_credentials_and_binary_frame_roundtrip(self):
        # A fake family map authorizes exactly the real test process UID; the
        # Handler must obtain that UID from SO_PEERCRED, never from request data.
        self.families.groups={os.getuid():('test-main','test-child')}
        with tempfile.TemporaryDirectory() as temporary:
            listener=socket.socket(socket.AF_UNIX);listener.bind(str(Path(temporary)/'socket'));listener.listen(4)
            with broker.Server(listener,self.cache) as server:
                thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
                try:
                    with socket.socket(socket.AF_UNIX) as connection:
                        connection.connect(str(Path(temporary)/'socket'))
                        broker.send(connection,broker.request(ord('U'),value=PNG))
                        self.assertEqual(broker.parse_response(broker.receive(connection))[2],PNG)
                    self.families.groups={}
                    with socket.socket(socket.AF_UNIX) as connection:
                        connection.connect(str(Path(temporary)/'socket'))
                        self.assertEqual(broker.parse_response(broker.receive(connection))[0],b'E')
                finally:server.shutdown();thread.join(3)


@unittest.skipUnless(os.geteuid()==0,'trusted local account fixtures')
class NativeFamilyMetadataTest(unittest.TestCase):
    def test_real_local_uid_mapping_rejects_alias_uid_and_changes_generation(self):
        with tempfile.TemporaryDirectory(dir='/root') as temporary:
            root=Path(temporary);config=root/'config';config.mkdir();passwd=root/'passwd'
            data='home:x:1001:1001:Home:/home/home:/bin/sh\nwork:x:1002:1002:Work:/home/work:/bin/sh\nother:x:1003:1003:Other:/home/other:/bin/sh\n'
            passwd.write_text(data);(config/'desktops.conf').write_text('home work\n')
            families=broker.Families(ConfigStore(config,root/'absent'),LocalAccounts(passwd))
            epoch,family=families.resolve(1001)
            self.assertEqual(families.resolve(1002),(epoch,family));self.assertIsNone(families.resolve(1003)[1])
            passwd.write_text(data+'alias:x:1002:1002:Alias:/home/alias:/bin/sh\n')
            self.assertIsNone(families.resolve(1002)[1]);self.assertNotEqual(families.resolve(1001)[0],epoch)
            passwd.write_text(data);(config/'desktops.conf').chmod(0o666)
            with self.assertRaises(ValueError):families.resolve(1001)


class ClientStateTest(unittest.TestCase):
    def setUp(self):self.state=client.Synchronizer();self.cache=broker.Clipboards(Families());self.writes=[]
    def cycle(self,current,blocked=False,changed=True,clear=False):
        reply=self.cache.exchange(1002,self.state.message(current,blocked,changed,clear))
        return self.state.accept(reply,current,blocked,self.writes.append)

    def test_first_client_seeds_existing_local_clipboard_once(self):
        self.assertEqual(self.cycle(TEXT),TEXT);self.assertFalse(self.writes)
        self.assertEqual(broker.parse_response(self.cache.exchange(1001,broker.request(ord('G'))))[2],TEXT)

    def test_new_client_pulls_latest_and_remote_echo_is_not_published(self):
        self.cache.exchange(1001,broker.request(ord('U'),value=PNG))
        self.assertEqual(self.cycle(TEXT),PNG);self.assertEqual(self.writes,[PNG])
        self.assertEqual(self.state.message(PNG,False,True)[1:2],b'G')
        self.assertEqual(self.cycle(TEXT),TEXT)

    def test_real_copy_during_startup_reply_supersedes_the_seed_instead_of_being_lost(self):
        reply=self.cache.exchange(1002,self.state.message(TEXT,False,True))
        self.assertTrue(self.state.newer_local_copy(TEXT,PNG))
        update=self.state.message(PNG,False,True)
        self.assertEqual(update[1:2],b'U')
        reply=self.cache.exchange(1002,update)
        self.assertEqual(self.state.accept(reply,PNG,False,self.writes.append),PNG)
        self.assertFalse(self.writes)
        other=broker.parse_response(self.cache.exchange(1001,broker.request(ord('G'))))
        self.assertEqual(other[2],PNG)

    def test_sensitive_and_unsupported_selections_neither_publish_nor_get_overwritten(self):
        self.cache.exchange(1001,broker.request(ord('U'),value=PNG))
        self.assertEqual(self.state.message(None,True,True)[1:2],b'G')
        self.assertIsNone(self.cycle(None,blocked=True));self.assertFalse(self.writes)
        self.assertEqual(self.state.known,broker.ZERO)
        self.assertEqual(self.cycle(TEXT),TEXT)

    def test_failed_local_write_does_not_acknowledge_remote_content(self):
        reply=self.cache.exchange(1001,broker.request(ord('U'),value=PNG))
        with self.assertRaises(OSError):self.state.accept(reply,TEXT,False,lambda value:(_ for _ in ()).throw(OSError()))
        self.assertEqual(self.state.known,broker.ZERO)
        self.assertEqual(self.state.accept(reply,TEXT,False,self.writes.append),PNG)

    def test_broker_restart_reseeds_without_erasing_current_clipboard(self):
        self.cycle(TEXT);self.cache.values.clear()
        self.assertEqual(self.cycle(TEXT,changed=False),TEXT)
        self.assertEqual(self.state.message(TEXT,False,False)[1:2],b'S')
        self.assertEqual(self.cycle(TEXT,changed=False),TEXT)

    def test_new_copy_supersedes_a_pending_restart_seed_from_another_client(self):
        self.cycle(TEXT);self.cache.values.clear()
        self.cycle(TEXT,changed=False)
        self.cache.exchange(1001,broker.request(ord('S'),value=TEXT))
        self.assertEqual(self.cycle(PNG),PNG)
        self.assertFalse(self.writes)
        self.assertEqual(broker.parse_response(self.cache.exchange(1001,broker.request(ord('G'))))[2],PNG)

    def test_explicit_clear_and_permission_loss_keep_distinct_semantics(self):
        self.cycle(TEXT)
        self.assertIsNone(self.cycle(TEXT,clear=True));self.assertEqual(self.writes,[b''])
        self.cache.families.groups[1002]=None
        self.assertEqual(self.cycle(TEXT),TEXT);self.assertTrue(self.state.initial)

    def test_corrupt_response_is_rejected_before_touching_local_selection(self):
        for response in (b'\1D'+broker.ZERO+PNG,b'\1C'+broker.ZERO,b'\1Z'+broker.ZERO+TEXT,b'\2N'+broker.ZERO):
            with self.assertRaises(ValueError):self.state.accept(response,TEXT,False,self.writes.append)
        self.assertFalse(self.writes)

    def test_bounded_reads_stop_large_or_stalled_producers(self):
        code,data=client.bounded_output([sys.executable,'-c','print("ok",end="")'],4)
        self.assertEqual((code,data),(0,b'ok'))
        with self.assertRaises(ValueError):client.bounded_output([sys.executable,'-c','print("too large")'],4)
        started=time.monotonic()
        with self.assertRaises(TimeoutError):client.bounded_output([sys.executable,'-c','import time;time.sleep(5)'],10,timeout=.1)
        self.assertLess(time.monotonic()-started,1)

    def test_sensitive_format_is_never_requested_and_uri_list_stays_local(self):
        for formats in (b'text/plain\nx-kde-passwordManagerHint\n',b'text/uri-list\n'):
            with patch.object(client,'bounded_output',return_value=(0,formats)) as process:
                self.assertEqual(client.clipboard(),(True,None));self.assertEqual(process.call_count,1)

    def test_canonical_png_and_legacy_utf8_offer_keep_exact_bytes(self):
        for formats,target,result in ((b'image/png\ntext/plain\n','image/png',PNG),(b'UTF8_STRING\n','UTF8_STRING',TEXT)):
            content=result.partition(b'\0')[2]
            with patch.object(client,'bounded_output',side_effect=[(0,formats),(0,content)]) as process:
                self.assertEqual(client.clipboard(),(False,result))
                self.assertEqual(process.call_args.args[0][-1],target)
