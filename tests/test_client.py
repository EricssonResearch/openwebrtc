"""
Use environment variable: OWR_USE_TEST_SOURCES=1
"""
import sys
import random
import time
from gi.repository import GLib
from gi.repository import Gio
from gi.repository import Owr
from gi.repository import Soup

SERVER_URL = "http://demo.openwebrtc.org:38080"

def eventstream_line_read(input_stream, result, _data):
    line = input_stream.read_line_finish_utf8(result)
    print("Got line of length: %d (%s)" % (line[1], line[0]))
    read_eventstream_line(input_stream)


def read_eventstream_line(input_stream):
    input_stream.read_line_async(GLib.PRIORITY_DEFAULT, None, eventstream_line_read, None)


def eventsource_request_sent(session, result, _data):
    print("request sent")
    input_stream = session.send_finish(result)
    if input_stream:
        data_input_stream = Gio.DataInputStream.new(input_stream)
        read_eventstream_line(data_input_stream)
    else:
        print("error")
        time.sleep(10)
        Owr.quit()


def send_eventsource_request(url):
    session = Soup.Session.new()
    message = Soup.Message.new("GET", url)
    print("got here: 1 " + url)
    session.send_async(message, None, eventsource_request_sent, None)
    print("got here: 2")


def got_local_sources(sources):
    print(sources)
    ta = Owr.TransportAgent.new(False)
    ta.add_helper_server(Owr.HelperServerType.STUN, "stun.services.mozilla.com", 3478, None, None)
    url = SERVER_URL + '/stoc/%s/%d' % (sys.argv[1], random.randint(0, pow(2, 32)-1))
    send_eventsource_request(url)
    print("got here: 3")


def main():
    mc = GLib.MainContext.get_thread_default()
    if not mc:
        mc = GLib.MainContext.default()
    Owr.init(mc)
    Owr.get_capture_sources(Owr.MediaType.VIDEO, got_local_sources)
    Owr.run()
    print("exiting")

if __name__ == '__main__':
    main()