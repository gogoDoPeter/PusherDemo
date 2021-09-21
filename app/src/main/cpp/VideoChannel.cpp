#include <rtmp.h>
#include "VideoChannel.h"
#include "util.h"

VideoChannel::VideoChannel() {
    pthread_mutex_init(&mutex, 0);
}

VideoChannel::~VideoChannel() {
    pthread_mutex_destroy(&mutex);
}

// 初始化 x264 编码器
void VideoChannel::initVideoEncoder(int width, int height, int fps, int bitrate) {
    // 防止编码器多次创建 互斥锁
    pthread_mutex_lock(&mutex);

    mWidth = width;
    mHeight = height;
    mFps = fps;
    mBitrate = bitrate;

    y_len = width * height;
    uv_len = y_len / 4;

    // 防止重复初始化
    if (videoEncoder) {
        x264_encoder_close(videoEncoder);
        videoEncoder = nullptr;
    }
    if (pic_in) {
        x264_picture_clean(pic_in);
        DELETE(pic_in);
    }

    // 初始化x264编码器
    x264_param_t param; // x264的参数集

    // 设置编码器属性
    // ultrafast 最快  （直播必须快）
    // zerolatency 零延迟（直播必须快）
    x264_param_default_preset(&param, "ultrafast", "zerolatency");

    // 编码规格：https://wikipedia.tw.wjbk.site/wiki/H.264 看图片
    param.i_level_idc = 32; // 3.2 中等偏上的规格  自动用 码率，模糊程度，分辨率

    // 输入数据格式是 YUV420P  平面模式VVVVVUUUU，如果没有P，  就是交错模式VUVUVUVU
    param.i_csp = X264_CSP_I420;
    param.i_width = width;
    param.i_height = height;

    // 不能有B帧，如果有B帧会影响编码、解码效率（快）
    param.i_bframe = 0;

    // 码率控制方式。CQP(恒定质量)，CRF(恒定码率)，ABR(平均码率)
    param.rc.i_rc_method = X264_RC_CRF;

    // 设置码率
    param.rc.i_bitrate = bitrate / 1000;

    // 瞬时最大码率 网络波动导致的
    param.rc.i_vbv_max_bitrate = bitrate / 1000 * 1.2;

    // 设置了i_vbv_max_bitrate就必须设置buffer大小，码率控制区大小，单位Kb/s
    param.rc.i_vbv_buffer_size = bitrate / 1000;

    // 码率控制不是通过 timebase 和 timestamp，码率的控制，完全不用时间戳   ，而是通过 fps 来控制 码率（根据你的fps来自动控制）
    param.b_vfr_input = 0;

    // 分子 分母
    // 帧率分子
    param.i_fps_num = fps;
    // 帧率分母
    param.i_fps_den = 1;
    param.i_timebase_den = param.i_fps_num;
    param.i_timebase_num = param.i_fps_den;

    // 告诉人家，到底是什么时候，来一个I帧， 计算关键帧的距离
    // 帧距离(关键帧)  2s一个关键帧   （就是把两秒钟一个关键帧告诉人家）
    param.i_keyint_max = fps * 2;

    // sps序列参数   pps图像参数集，所以需要设置header(sps pps)
    // 是否复制sps和pps放在每个关键帧的前面 该参数设置是让每个关键帧(I帧)都附带sps/pps。
    param.b_repeat_headers = 1;

    // 并行编码线程数
    param.i_threads = 1;

    // profile级别，baseline级别 (把我们上面的参数进行提交)
    x264_param_apply_profile(&param, "baseline");

    // 输入图像初始化
    pic_in = new x264_picture_t; // 本身空间的初始化
    x264_picture_alloc(pic_in, param.i_csp, param.i_width, param.i_height); // pic_in内部成员初始化等

    // 打开编码器 一旦打开成功，我们的编码器就拿到了
    // 打开编码器
    videoEncoder = x264_encoder_open(&param);
    if (videoEncoder) {
        LOGE("x264编码器打开成功");
    }

    pthread_mutex_unlock(&mutex);
}

/**
 * 编码工作
 * @param data
 */
void VideoChannel::encodeData(signed char *data) {
    pthread_mutex_lock(&mutex);

    //TODO convert nv21 to i420
    // 把nv21的y分量 Copy i420的y分量
    memcpy(pic_in->img.plane[0], data, y_len);

    //TODO libyuv旋转功能  交叉编译 后面实现

    for (int i = 0; i < uv_len; ++i) {
        // u 数据
        // data + y_len + i * 2 + 1 : 移动指针取 data(nv21) 中 u 的数据
        *(pic_in->img.plane[1] + i) = *(data + y_len + i * 2 + 1);

        // v 数据
        // data + y_len + i * 2 ： 移动指针取 data(nv21) 中 v 的数据
        *(pic_in->img.plane[2] + i) = *(data + y_len + i * 2);
    }

    x264_nal_t *nal = nullptr; // 通过H.264编码得到NAL数组（理解）
    int pi_nal; // pi_nal是nal中输出的NAL单元的数量
    x264_picture_t pic_out; // 输出编码后图片 （编码后的图片）

    // 1.视频编码器， 2.nal，  3.pi_nal是nal中输出的NAL单元的数量， 4.输入原始的图片，  5.输出编码后图片
    int ret = x264_encoder_encode(videoEncoder, &nal, &pi_nal, pic_in, &pic_out); // 进行编码（本质的理解是：编码一张图片）
    if (ret < 0) { // 返回值：x264_encoder_encode函数 返回返回的 NAL 中的字节数。如果没有返回 NAL 单元，则在错误时返回负数和零。
        LOGE("x264编码失败");
        pthread_mutex_unlock(&mutex); // 同学们注意：一旦编码失败了，一定要解锁，否则有概率性造成死锁了
        return;
    }

    // 发送 Packets 入队queue
    // sps(序列参数集) pps(图像参数集) 说白了就是：告诉我们如何解码图像数据
    int sps_len, pps_len; // sps 和 pps 的长度
    uint8_t sps[100]; // 用于接收 sps 的数组定义
    uint8_t pps[100]; // 用于接收 pps 的数组定义
    pic_in->i_pts += 1; // pts显示的时间（+=1 目的是每次都累加下去）， dts编码的时间

    for (int i = 0; i < pi_nal; ++i) {
        if (nal[i].i_type == NAL_SPS) {
            sps_len = nal[i].i_payload - 4; // 去掉起始码（之前我们学过的内容：00 00 00 01） TODO 播放的时候，自己会加
            memcpy(sps, nal[i].p_payload + 4, sps_len); // 由于上面减了4，所以+4挪动这里的位置开始
        } else if (nal[i].i_type == NAL_PPS) {
            pps_len = nal[i].i_payload - 4; // 去掉起始码 之前我们学过的内容：00 00 00 01）
            memcpy(pps, nal[i].p_payload + 4, pps_len); // 由于上面减了4，所以+4挪动这里的位置开始

            // sps + pps
            sendSpsPps(sps, pps, sps_len, pps_len); // pps是跟在sps后面的，这里拿到的pps表示前面的sps肯定拿到了
        } else {
            // 发送 I帧 P帧
            sendFrame(nal[i].i_type, nal[i].i_payload, nal[i].p_payload);
        }
    }

    pthread_mutex_unlock(&mutex);
}

// 把sps + pps 存入队列
void VideoChannel::sendSpsPps(uint8_t *sps, uint8_t *pps, int sps_len, int pps_len) {

    // 图已经OK，对图来
    int body_size = 5 + 8 + sps_len + 3 + pps_len;

    RTMPPacket *packet = new RTMPPacket; // 开始封包RTMPPacket

    RTMPPacket_Alloc(packet, body_size); // 堆区实例化 RTMPPacket

    int i = 0;
    packet->m_body[i++] = 0x17; // 十六进制转换成二进制，二进制查表 就懂了

    packet->m_body[i++] = 0x00;   // 重点是此字节 如果是1 帧类型（关键帧 非关键帧）， 如果是0一定是 sps pps
    packet->m_body[i++] = 0x00;
    packet->m_body[i++] = 0x00;
    packet->m_body[i++] = 0x00;

    // 看图说话
    packet->m_body[i++] = 0x01; // 版本

    packet->m_body[i++] = sps[1];
    packet->m_body[i++] = sps[2];
    packet->m_body[i++] = sps[3];

    packet->m_body[i++] = 0xFF;
    packet->m_body[i++] = 0xE1;

    // 两个字节表达一个长度，需要位移
    // 用两个字节来表达 sps的长度，所以就需要位运算，取出sps_len高8位 再取出sps_len低8位
    //（同学们去看看位运算：https://blog.csdn.net/qq_31622345/article/details/98070787）
    // https://www.cnblogs.com/zhu520/p/8143688.html
    packet->m_body[i++] = (sps_len >> 8) & 0xFF; // 取高8位
    packet->m_body[i++] = sps_len & 0xFF; // 取低8位

    memcpy(&packet->m_body[i], sps, sps_len); // sps拷贝进去了

    i += sps_len; // 拷贝完pps数据 ，i移位，（下面才能准确移位）

    packet->m_body[i++] = 0x01; // pps个数，用一个字节表示

    // 两个字节表达一个长度，需要位移
    // 用两个字节来表达 pps的长度，所以就需要位运算，取出pps_len高8位 再取出pps_len低8位
    //（同学们去看看位运算：https://blog.csdn.net/qq_31622345/article/details/98070787）
    // https://www.cnblogs.com/zhu520/p/8143688.html
    packet->m_body[i++] = (pps_len >> 8) & 0xFF; // 取高8位
    packet->m_body[i++] = pps_len & 0xFF; // 去低8位

    memcpy(&packet->m_body[i], pps, pps_len); // pps拷贝进去了

    i += pps_len; // 拷贝完pps数据 ，i移位，（下面才能准确移位）

    // 封包处理
    packet->m_packetType = RTMP_PACKET_TYPE_VIDEO; // 包类型 视频包
    packet->m_nBodySize = body_size; // 设置好 sps+pps的总大小
    packet->m_nChannel = 10; // 通道ID，随便写一个，注意：不要写的和 rtmp.c (里面的m_nChannel有冲突 4301行)
    packet->m_nTimeStamp = 0; // sps pps 包 没有时间戳
    packet->m_hasAbsTimestamp = 0; // 时间戳绝对或相对 也没有时间戳
    packet->m_headerType = RTMP_PACKET_SIZE_MEDIUM ; // 包的类型：数据量比较少，不像帧数据(那就很大了)，所以设置中等大小的包

    // packet 存入队列
    videoCallback(packet);
}

void VideoChannel::setVideoCallback( VideoCallback videoCallback) {
    this->videoCallback = videoCallback;
}

/* 发送帧信息
* @param type 帧类型
* @param payload 帧数据长度
* @param pPayload 帧数据
*/
void VideoChannel::sendFrame(int type, int payload, uint8_t *pPayload) {
    // 去掉起始码 00 00 00 01 或者 00 00 01
    if (pPayload[2] == 0x00){ // 00 00 00 01
        pPayload += 4; // 例如：共10个，挪动4个后，还剩6个
        // 保证 我们的长度是和上的数据对应，也要是6个，所以-= 4
        payload -= 4;
    }else if(pPayload[2] == 0x01){ // 00 00 01
        pPayload +=3; // 例如：共10个，挪动3个后，还剩7个
        // 保证 我们的长度是和上的数据对应，也要是7个，所以-= 3
        payload -= 3;
    }

    // 图已经OK，对图来
    int body_size = 5 + 4 + payload;

    RTMPPacket *packet = new RTMPPacket; // 开始封包RTMPPacket

 /*   Student *ss=new Student();
    delete ss;*/
    RTMPPacket_Alloc(packet, body_size); // 堆区实例化 RTMPPacket

    // 区分关键帧 和 非关键帧
    packet->m_body[0] = 0x27; // 普通帧 非关键帧
    if(type == NAL_SLICE_IDR){
        packet->m_body[0] = 0x17; // 关键帧
    }

    packet->m_body[1] = 0x01; // 重点是此字节 如果是1 帧类型（关键帧 非关键帧）， 如果是0一定是 sps pps
    packet->m_body[2] = 0x00;
    packet->m_body[3] = 0x00;
    packet->m_body[4] = 0x00;

    // 四个字节表达一个长度，需要位移
    // 用四个字节来表达 payload帧数据的长度，所以就需要位运算
    //（同学们去看看位运算：https://blog.csdn.net/qq_31622345/article/details/98070787）
    // https://www.cnblogs.com/zhu520/p/8143688.html
    packet->m_body[5] = (payload >> 24) & 0xFF;
    packet->m_body[6] = (payload >> 16) & 0xFF;
    packet->m_body[7] = (payload >> 8) & 0xFF;
    packet->m_body[8] = payload & 0xFF;

    memcpy(&packet->m_body[9], pPayload, payload); // 拷贝H264的裸数据

    packet->m_packetType = RTMP_PACKET_TYPE_VIDEO; // 包类型，是视频类型
    packet->m_nBodySize = body_size; // 设置好 关键帧 或 普通帧 的总大小
    packet->m_nChannel = 10; // 通道ID，随便写一个，注意：不要写的和rtmp.c(里面的m_nChannel有冲突 4301行)
    packet->m_nTimeStamp = -1; // 帧数据有时间戳
    packet->m_hasAbsTimestamp = 0; // 时间戳绝对或相对 用不到，不需要
    packet->m_headerType = RTMP_PACKET_SIZE_LARGE ; // 包的类型：若是关键帧的话，数据量比较大，所以设置大包

    // 把最终的 帧类型 RTMPPacket 存入队列
    videoCallback(packet);
}


