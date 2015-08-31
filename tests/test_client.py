import sys
import random
from gi.repository import GLib
from gi.repository import Owr
from gi.repository import Soup

SERVER_URL = "http://demo.openwebrtc.org:38080"


def eventsource_request_sent(session, result):
    pass

def send_eventsource_request(url):
    session = Soup.Session.new()
    message = Soup.Message.new("GET", url)
    session.send_async(message, None, eventsource_request_sent)
    print("got here")
    Owr.quit()


def got_local_sources(sources):
    print(sources)
    ta = Owr.TransportAgent.new(False)
    ta.add_helper_server(Owr.HelperServerType.STUN, "stun.services.mozilla.com", 3478, None, None)
    url = SERVER_URL + '/stoc/%s/%d' % (sys.argv[1], random.randint(0, pow(2, 32)-1))
    send_eventsource_request(url)


def main():
    mc = GLib.MainContext.new()
    Owr.init(mc)
    Owr.get_capture_sources(Owr.MediaType.VIDEO, got_local_sources)
    Owr.run()
    print("exiting")

if __name__ == '__main__':
    main()