<img src="http://static.squarespace.com/static/53f1eedee4b0439bf8d480c5/t/54061d4ae4b0f4290347d846/1411419445727/?format=1500w" alt="OWR Logo" width="200" height="126">

A flexible, mobile-first, cross-platform WebRTC client framework based on [GStreamer](http://gstreamer.freedesktop.org). OpenWebRTC currently supports iOS, Android, Mac OS X and Linux. Windows support is quite doable too if needed.

## Releases
[Downloadable binary releases are available here](https://github.com/EricssonResearch/openwebrtc/releases).

## API Documentation
[API documentation is available here](http://ericssonresearch.github.io/openwebrtc/docs/gtk-doc/dev/).

## Examples using OpenWebRTC
Bowser is a mobile browser that uses OpenWebRTC as a WebRTC back-end. As you can see [here](https://github.com/EricssonResearch/bowser), Bowser is in fact a very thin layer of UI code on top of OpenWebRTC.

We are also writing some [example code and applications](https://github.com/EricssonResearch/openwebrtc-examples) to help you get started using OpenWebRTC on various platforms.

Before being released publicly, OpenWebRTC has been used by Ericsson Research to build several research prototypes such as [this](http://www.ericsson.com/research-blog/context-aware-communication/field-service-support-google-glass-webrtc/) and [that](http://www.ericsson.com/research-blog/5g/remote-excavation-using-webrtc-real-time-video-eye-5g/). 

## Building
Take a look at the [Building OpenWebRTC](https://github.com/EricssonResearch/openwebrtc/wiki/Building-OpenWebRTC) wiki page for information about how to build the framework.

## Community
For support, questions and discussions:
* Public [mailing list](https://groups.google.com/forum/#!forum/openwebrtc)
* Twitter: [@OpenWebRTC](https://twitter.com/OpenWebRTC)
* [IRC](http://webchat.freenode.net/?channels=openwebrtc) - irc://chat.freenode.net/openwebrtc
* [OpenWebRTC blog](http://www.openwebrtc.org/blog/) (maintained by Ericsson Research)
* [Issue tracker](https://github.com/EricssonResearch/openwebrtc/issues)

## License
OpenWebRTC is released under BSD-2 clause. See LICENSE for details.

## Background

OpenWebRTC is built on the belief that the [WebRTC standard](http://www.w3.org/2011/04/webrtc/) would transcend the pure browser environment and that native apps, implementing the same protocols and API's, would become an important part of the WebRTC ecosystem. This is especially true on mobile platforms where native app distribution is often preferred over pure web apps. Native OpenWebRTC apps can either talk to other native apps or browsers that support WebRTC. OpenWebRTC can also provide the WebRTC-backend to web browsers.

Having independent, interoperable, implementations is important for the health of any standard, and WebRTC is no exception. The ambition of OpenWebRTC is to follow the WebRTC standard closely as it continues to evolve. 

## Architecture
OpenWebRTC was designed for flexibility and modularity. The bulk of the API layer is implemented in JavaScript, making it super fast to modify and extend with new functionality. Below is a simplified sketch of the architecture.

<img src="http://static.squarespace.com/static/53f1eedee4b0439bf8d480c5/t/54241e32e4b04e698dffecec/1411653170102/Arch.png" alt="Simplified architecture" width="445" height="247">

Applications built on top of OpenWebRTC will be interoperable with popular WebRTC-enabled browsers such as Chrome and Firefox.
