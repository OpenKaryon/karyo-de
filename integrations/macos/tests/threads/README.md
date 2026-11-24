# Ordinary Darwin pthread mask client

Build this explicit client with Builder's Mac probe compiler and `--suite threads`.
It creates and joins four normal Darwin pthreads using the four combinations of
SIGCHLD/SIGTTOU mask membership. Each worker queries its inherited public mask,
obtains and releases a real Mach thread port and returns its original argument.
The creator checks its own mask after each join and restores its original mask.

The client neither sends signals nor induces failures. Run it through the normal
installed launcher as the ordinary QA user and retain its JSON and actual exit
status. On runtime63 this separately checks Darwin-thread restoration after the
native creation boundary. The Linux DSO worker check covers another contract;
it does not establish this Darwin result. Compilation does not imply execution,
and a passing pthread client does not prove original UGENE GUI compatibility.
