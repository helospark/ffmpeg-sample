g++ -O0 -g -w  hwdecode.cpp -fpermissive -o hwdecode.out `pkg-config --libs libavcodec libavformat libavutil libswscale`
g++ -O0 -g -w  swdecode.cpp -fpermissive -o swdecode.out `pkg-config --libs libavcodec libavformat libavutil libswscale`
