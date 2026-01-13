# ffmpeg -i output.exr -vf format=rgb24,tonemap=linear output-lll.png
ffmpeg -y -i output.exr -pix_fmt rgb48le output-exr.png
ffmpeg -y -i output.ppm -pix_fmt rgb24 output.png
ffmpeg -y -i output-g.ppm -pix_fmt rgb24 output-g.png