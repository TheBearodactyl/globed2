# server changelog

least chaotic thing about this repo tbh

## game v1.1.0

Protocol version is bumped to v2, so people on older versions of the mod won't be able to connect anymore.

The game server now no longer uses only UDP, and instead uses a hybrid of TCP and UDP (on the same port). This means that if you used to do port forwarding, you now need to forward the port for both TCP and UDP.

## central v1.1.0

* bumped the protocol version to v2 to be compatible with the game server update.
* change the way account verification is done, removed some of the related settings and added new ones.
* allow writing comments in the JSON configuration file

## game v1.0.2

* add two new packets: `ConnectionTestPacket` and `ConnectionTestResponsePacket`. used for testing the connection.

## central v1.0.1

* change the template game server ip address from `127.0.0.0` to `127.0.0.1` (oops)

## game v1.0.1

* don't include unauthenticated players in the room packet
