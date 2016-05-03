"""
Use environment variable: OWR_USE_TEST_SOURCES=1
"""
import base64
import hashlib
import json
import re
import sys
import random
import time
from gi.repository import GLib
from gi.repository import Gio
from gi.repository import Owr
from gi.repository import Soup

SERVER_URL = "http://demo.openwebrtc.org"

ALL_SESSIONS = []
LOCAL_SOURCES = []
TRANSPORT_AGENT = None
CLIENT_ID = 0
PEER_ID = ""
CANDIDATE_TYPES = {
    Owr.CandidateTypes.HOST: "host",
    Owr.CandidateTypes.SRFLX: "srflx",
    Owr.CandidateTypes.PRFLX: "prflx",
    Owr.CandidateTypes.RELAY: "relay"}
TCP_TYPES = {
    Owr.TransportTypes.TCP_ACTIVE: "active",
    Owr.TransportTypes.TCP_PASSIVE: "passive",
    Owr.TransportTypes.TCP_SO: "so"}


def answer_sent(session, result):
    input_stream = session.send_finish(result)
    if not input_stream:
        print("Failed to send answer to server")
    print("Answer Sent")


def can_send_answer():
    for _, session_data in ALL_SESSIONS:
        if not session_data.get('gathering-done', False) or not session_data.get('fingerprint', None):
            print session_data
            return False
    return True

def send_answer():
    print("Sending answer")
    answer = {}
    answer["type"] = "answer"
    sd = answer["sessionDescription"] = {}
    md = sd["mediaDescriptions"] = []
    for session, session_data in ALL_SESSIONS:
        media_description = { 
            "type": session_data["media-type"],
            "rtcp": { "mux": session.props.rtcp_mux }}
        payload = {
            "encodingName": session_data["encoding-name"],
            "type": session_data["payload-type"],
            "clockRate": session_data["clock-rate"] }
        if session_data["media-type"] == 'audio':
            payload["channels"] = session_data["channels"]
        elif session_data["media-type"] == "video":
            payload["ccmfir"] = session_data["ccm-fir"]
            payload["nackpli"] = session_data["nack-pli"]
        media_description["payloads"] = [payload]
        candidates = session_data["local-candidates"]
        media_description["ice"] = {
            "ufrag": candidates[0].props.ufrag,
            "password": candidates[0].props.password}
        candidates_array = []
        for candidate in candidates:
            candidate_dict = {
                "foundation": candidate.props.foundation,
                "componentId": int(candidate.props.component_type),
                "transport": "UDP" if candidate.props.transport_type == Owr.TransportTypes.UDP else "TCP",
                "priority": candidate.props.priority,
                "address": candidate.props.address,
                "port": candidate.props.port,
                "type": CANDIDATE_TYPES[candidate.props.type]
                }
            if candidate.props.type != Owr.CandidateTypes.HOST:
                candidate_dict["relatedAddress"] = candidate.props.base_address
                candidate_dict["relatedPort"] = candidate.props.base_port
            if candidate.props.transport_type != Owr.TransportTypes.UDP:
                candidate_dict["tcpType"] = TCP_TYPES[candidate.props.transport_type]
            candidates_array.append(candidate_dict)
        media_description["ice"]["candidates"] = candidates_array
        media_description["dtls"] = {
            "fingerprintHashFunction": "sha-256",
            "fingerprint": session_data["fingerprint"],
            "setup": "active"
        }
        md.append(media_description)
    url = "%s/ctos/%s/%u/%s" % (SERVER_URL, sys.argv[1], CLIENT_ID, PEER_ID)
    message_data = json.dumps(answer)
    print("Answer: %s to url: %s" % (message_data, url))
    soup_session = Soup.Session.new()
    soup_message = Soup.Message.new("POST", url)
    soup_message.set_request("application/json", Soup.MemoryUse.COPY, message_data)
    soup_session.send_async(soup_message, None, answer_sent)


def find_session_data(session):
    for session_obj, session_data in ALL_SESSIONS:
        if session == session_obj:
            return session_data
    return None


def got_remote_source(session, source):
    print("Got remote source %s" % (source.props.name))
    media_type = source.props.media_type
    if media_type == Owr.MediaType.AUDIO:
        renderer = Owr.AudioRenderer.new()
    elif media_type == Owr.MediaType.VIDEO:
        renderer = Owr.ImageRenderer.new()
    renderer.set_source(source)


def got_candidate(session, candidate):
    print("Got candidate %s" % (candidate,))
    session_data = find_session_data(session)
    local_candidates = session_data.get("local-candidates", [])
    local_candidates.append(candidate)
    session_data["local-candidates"] = local_candidates


def candidate_gathering_done(session):
    print("Candidate gathering done")
    session_data = find_session_data(session)
    session_data['gathering-done'] = True
    if can_send_answer():
        send_answer()
    else:
        print("Not ready to send answer")


def got_dtls_certificate(session, pspec):
    print("Got DTLS Certificate")
    pem = session.props.dtls_certificate
    pem_b64 = ''
    lines = pem.split('\n')
    for line in lines:
        if not line.startswith('-----'):
            pem_b64 += line
        elif "END CERTIFICATE" in line:
            break
    print("Certificate: %s Stripped: %s" % (lines, pem_b64))
    pem_binary = base64.decodestring(pem_b64)
    checksum = hashlib.sha256(pem_binary)
    digest = checksum.hexdigest()
    fingerprint = ':'.join(re.findall('..',digest))
    session_data = find_session_data(session)
    if session_data:
        session_data['fingerprint'] = fingerprint
        if can_send_answer():
            send_answer()
        else:
            print("Not ready to send answer")
    else:
        print("ERROR: CANNOT FIND SESSION")


def reset():
    global LOCAL_SOURCES, TRANSPORT_AGENT, ALL_SESSIONS
    print("Reset")
    for session, session_data in ALL_SESSIONS:
        session.set_send_source(None)
    ALL_SESSIONS = []
    TRANSPORT_AGENT = None
    LOCAL_SOURCES = []
    Owr.get_capture_sources(Owr.MediaType.VIDEO, got_local_sources)


def candidate_from_description(candidate_description):
    candidate_type = candidate_description['type']
    if candidate_type == 'host':
        candidate_type = Owr.CandidateType.HOST
    elif candidate_type == 'srflx':
        candidate_type = Owr.CandidateType.SERVER_REFLEXIVE
    else:
        candidate_type = Owr.CandidateType.RELAY
    component_type = int(candidate_description['componentId'])
    remote_candidate = Owr.Candidate.new(candidate_type, component_type)
    foundation = candidate_description['foundation']
    remote_candidate.props.foundation = foundation
    transport = candidate_description['transport']
    if transport == 'UDP':
        transport = Owr.TransportType.UDP
    else:
        transport = Owr.TransportType.TCP_ACTIVE

    if transport != Owr.TransportType.UDP:
        tcp_type = candidate_description['tcpType']
        if tcp_type == 'active':
            transport = Owr.TransportType.TCP_ACTIVE
        elif tcp_type == 'passive':
            transport = Owr.TransportType.TCP_PASSIVE
        else:
            transport = Owr.TransportType.TCP_SO
    remote_candidate.props.transport_type = transport
    remote_candidate.props.address = candidate_description["address"]
    remote_candidate.props.port = int(candidate_description["port"])
    remote_candidate.props.priority = int(candidate_description["priority"])
    return remote_candidate


def handle_offer(message):
    print("Handling sdp offer")
    data = json.loads(message)
    media_descriptions = data["sessionDescription"]["mediaDescriptions"]
    for description in media_descriptions:
        session = Owr.MediaSession.new(True)
        media_type = description["type"]
        session_data = {}
        session_data['media-type'] = media_type
        session.props.rtcp_mux = bool(description["rtcp"]["mux"])
        payloads = description["payloads"]
        codec_type = Owr.CodecType.NONE
        for payload in payloads:
            encoding_name = payload["encodingName"]
            payload_type = int(payload["type"])
            clock_rate = int(payload["clockRate"])
            send_payload = None
            receive_payload = None
            if media_type == 'audio':
                media_type = Owr.MediaType.AUDIO
                if encoding_name == 'PCMA':
                    codec_type = Owr.CodecType.PCMA
                elif encoding_name == 'PCMU':
                    codec_type = Owr.CodecType.PCMU
                elif encoding_name == 'OPUS':
                    codec_type = Owr.CodecType.OPUS
                else:
                    continue
                channels = int(payload["channels"])
                send_payload = Owr.AudioPayload.new(codec_type, payload_type, clock_rate, channels)
                receive_payload = Owr.AudioPayload.new(codec_type, payload_type, clock_rate, channels)
            elif media_type == 'video':
                media_type = Owr.MediaType.VIDEO
                if encoding_name == 'H264':
                    codec_type = Owr.CodecType.H264
                elif encoding_name == 'VP8':
                    codec_type = Owr.CodecType.VP8
                else:
                    continue
                ccm_fir = bool(payload["ccmfir"])
                nack_pli = bool(payload["nackpli"])
                send_payload = Owr.VideoPayload.new(codec_type, payload_type, clock_rate, ccm_fir, nack_pli)
                receive_payload = Owr.VideoPayload.new(codec_type, payload_type, clock_rate, ccm_fir, nack_pli)
            else:
                print("Media type: %s not supported" % (media_type,))
                continue

            if send_payload and receive_payload:
                session_data['encoding-name'] = encoding_name
                session_data['payload-type'] = payload_type
                session_data['clock-rate'] = clock_rate
                if media_type == Owr.MediaType.AUDIO:
                    session_data['channels'] = channels
                elif media_type == Owr.MediaType.VIDEO:
                    session_data['ccm-fir'] = ccm_fir
                    session_data['nack-pli'] = nack_pli
                session.add_receive_payload(receive_payload)
                session.set_send_payload(send_payload)
                break

        ice_ufrag = description["ice"]["ufrag"]
        session_data['remote-ice-ufrag'] = ice_ufrag
        ice_password = description["ice"]["password"]
        session_data['remote-ice-password'] = ice_password
        for candidate in description["ice"].get("candidates", []):
            remote_candidate = candidate_from_description(candidate)
            remote_candidate.props.ufrag = ice_ufrag
            remote_candidate.props.password = ice_password
            component_type = remote_candidate.props.component_type
            if not session.props.rtcp_mux or component_type != Owr.ComponentTypes.RTCP:
                print("Adding remote candidate from offer: %s" % (candidate,))
                session.add_remote_candidate(remote_candidate)
        session.connect("on-incoming-source", got_remote_source)
        session.connect("on-new-candidate", got_candidate)
        session.connect("on-candidate-gathering-done", candidate_gathering_done)
        session.connect("notify::dtls-certificate", got_dtls_certificate)

        for source in LOCAL_SOURCES:
            if media_type == source.props.media_type:
                session.set_send_source(source)
        ALL_SESSIONS.append((session, session_data))
        TRANSPORT_AGENT.add_session(session)


def handle_remote_candidate(message):
    print("Handling remote candidate")
    data = json.loads(message)
    sdp_mline_index = data["candidate"]["sdpMLineIndex"]
    candidate_description = data["candidate"]["candidateDescription"]
    remote_candidate = candidate_from_description(candidate_description)
    # stuff related to the media session
    media_session, session_data = ALL_SESSIONS[sdp_mline_index]
    ice_ufrag = session_data["remote-ice-ufrag"]
    ice_password = session_data["remote-ice-password"]
    remote_candidate.props.ufrag = ice_ufrag
    remote_candidate.props.password = ice_password
    if not media_session.props.rtcp_mux or remote_candidate.props.component_type != Owr.ComponentTypes.RTCP:
        print("Adding remote candidate: %s" % (candidate_description,))
        media_session.add_remote_candidate(remote_candidate)


def eventstream_line_read(input_stream, result, peer_joined):
    global PEER_ID
    line = input_stream.read_line_finish_utf8(result)
    if line[0]:
        if peer_joined and line[0].startswith('data:'):
            peer_joined = False
            PEER_ID = line[0][5:]
            print("Peer joined: " + PEER_ID)
        elif line[0].startswith('event:leave'):
            print("Peer left")
            PEER_ID = ''
            reset()
        elif line[0].startswith('event:join'):
            peer_joined = True
        elif line[0][7:].startswith('sdp'):
            handle_offer(line[0][5:])
        elif line[0][7:].startswith('candidate'):
            handle_remote_candidate(line[0][5:])
    read_eventstream_line(input_stream, peer_joined)


def read_eventstream_line(input_stream, peer_joined=False):
    input_stream.read_line_async(GLib.PRIORITY_DEFAULT, None, eventstream_line_read, peer_joined)


def eventsource_request_sent(session, result, _data):
    print("request sent")
    input_stream = session.send_finish(result)
    if input_stream:
        data_input_stream = Gio.DataInputStream.new(input_stream)
        read_eventstream_line(data_input_stream)
    else:
        print("error")
        Owr.quit()


def send_eventsource_request(url):
    session = Soup.Session.new()
    message = Soup.Message.new("GET", url)
    session.send_async(message, None, eventsource_request_sent, None)


def got_local_sources(sources):
    global LOCAL_SOURCES
    global TRANSPORT_AGENT
    global CLIENT_ID
    LOCAL_SOURCES = sources
    print(sources)
    ta = Owr.TransportAgent.new(False)
    ta.add_helper_server(Owr.HelperServerType.STUN, "stun.services.mozilla.com", 3478, None, None)
    TRANSPORT_AGENT = ta
    CLIENT_ID = random.randint(0, pow(2, 32)-1)
    url = SERVER_URL + '/stoc/%s/%d' % (sys.argv[1], CLIENT_ID)
    send_eventsource_request(url)


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
