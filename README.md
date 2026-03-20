# HTTPSTIME Time Source for ESPHome

_Copilot helped write this code._

## What

`httpstime` is a reasonably-working but naive and not-quite-conforming implementation of
the client portion of [PHK](https://phk.freebsd.dk/)'s
[HTTPSTIME](https://phk.freebsd.dk/time/20151129/) specification for [ESPHome](https://esphome.io/).
It sets the system clock based on HTTP responses from HTTP servers.

It is intended to be used in situations where SNTP is not available, but access
to HTTP(S) servers (preferably ones implementing the
[improved timekeeping response](https://phk.freebsd.dk/time/20151129/#improved-timekeeping-reponse)
part of the specification) is.

> [!IMPORTANT]
> This basically translates to _"you are extremely highly unlikely to actually need this
> time source"_. You should be using [SNTP](https://esphome.io/components/time/sntp/) instead.

> [!CAUTION]
> This `httpstime` time source implementation is potentially not very precise. Depending on
> circumstances you shall expect your clock to be off by several seconds (then again, depending
> on circumstances, it may only be several tens of milliseconds off).
>
> You shall also note that `httpstime` _sets_ the clock as opposed to _disciplining_ it.
> You shall expect hard jumps in your clock.
>
> You _will_ want to read the specification.

## Main points of non-conformance

The reasons are mainly down to the fact that [`http_request`](https://esphome.io/components/http_request/)
cannot (as far as I can tell at least) be controlled finely enough to (sensibly)
implement some requirements.

1. Only one request is issued and the result (+ RTT/2) is used as the reference time.
1. An "unwilling server" is neither recorded nor excluded from future communication
   attempts

## Configuration

Add the following snippet to your `config.yaml`:

```
external_components:
  - source:
      type: git
      url: https://github.com/melak/esphome-component-httpstime
      path: components
    components:
      - httpstime
```

Example configuration entry:

```
time:
  - platform: httpstime
    id: time_source
    timezone: Europe/Sofia
    url: http://example.com/.well-known/time
```

### Configuration variables

- **url** (*Required*): The URL to query for the current time. Any valid HTTP response is sufficient,
  but an endpoint implementing the [improved timekeeping response](https://phk.freebsd.dk/time/20151129/#improved-timekeeping-reponse)
  gives _considerably_ more accurate results.

  A non-TLS HTTP endpoint (i.e. `http://`, not `https://`) may improve accuracy
  (make your own threat assessment).

- **update_interval** (*Optional*): Defaults to `12h`.

- All options from [time](https://esphome.io/components/time/)

## Miscellanea

[mod_https_time](https://github.com/danielluke/mod_https_time) for Apache might be useful.

A sample implementation of the improved timekeeping response in Python using Flask might be something
along the lines of

```
from flask import Flask, Response
import time

app = Flask(__name__)

@app.route('/.well-known/time', methods = [ 'HEAD', 'GET' ])
def tell_time():
    t = time.time()
    return Response(
            None,
            status = 204,
            mimetype = 'text/plain',
            headers = {
                'X-HTTPSTIME': round(t, 6),
                'Cache-Control': 'no-store, no-cache, must-revalidate, post-check=0, pre-check=0, max-age=0',
                'Pragma': 'no-cache',
                'Expires': '-1',
            }
    )
```

Putting this to actual use and configuring one's HTTP server of choice is left as an exercise
to the reader.

Technically, returning a 400-class response would be better from a cache control perspective,
but apparently ESPHome likes to log those "failed" requests in unfriendly red letters,and I
tend not to want to see them while also not wanting to figure out how to configure `http_request`
logging appropriately, so here we are.
