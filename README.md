# DSCamera

A DirectShow Sample Grabber Example

```
Usage: DSCamera
Options:
 --device                specify device index (default=0)
 --type {YUY2,NV12,MJPG} request video type (default=YUY2)
 --stream                specify video stream index (default=0)
 --width                 request video width (default=320)
 --height                request video height (default=240)
 --framerate             request video frame rate (default=30)
 --help                  This message

Example:
 DSCamera --device=0 --type=YUY2 --framerate=30
```