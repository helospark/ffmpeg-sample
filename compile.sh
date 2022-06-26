g++ -O0 -g -w  hwdecode.cpp -fpermissive -o hwdecode.out `pkg-config --libs libavcodec libavformat libavutil libswscale  libavfilter`
g++ -O0 -g -w  hwdecode_without_filter.cpp -fpermissive -o hwdecode_without_filter.out `pkg-config --libs libavcodec libavformat libavutil libswscale  libavfilter`
g++ -O0 -g -w  swdecode.cpp -fpermissive -o swdecode.out `pkg-config --libs libavcodec libavformat libavutil libswscale`
#g++ -O0 -g -w avfiltersample.cpp -fpermissive -o avfilter.out `pkg-config --libs libavcodec libavformat libavutil libswscale libavfilter`
