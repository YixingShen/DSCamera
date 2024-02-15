# DSCamera

A DirectShow Sample Grabber Example

```
Usage: DSCamera
Options:
 --device DEVICE          specify device index (default=0)
 --type {YUY2,NV12,MJPG}  request video type (default=YUY2)
 --stream STREAM          specify video stream index (default=0)
 --width WIDTH            request video width (default=320)
 --height HEIGHT          request video height (default=240)
 --framerate FRAMERATE    request video frame rate (default=30)
 --help                   help message

Example:
 DSCamera --device=0 --type=YUY2 --framerate=30
```