package com.lezhitech.pusherdemo;

import android.app.Activity;
import android.view.SurfaceHolder;

// TODO 中转站，只有他有资格和 C++层打交道
public class WePusher {
    static {
        System.loadLibrary("native-lib");
    }

    private VideoChannel videoChannel;
    // private AudioChannel audioChannel;

    // 此中转站的构造，主要是的三件事，①:初始化native层需要的加载， ②:实例化视频通道并传递基本参数(宽高,fps,码率等)， ③:实例化视频通道
    public WePusher(Activity activity, int cameraId, int width, int height, int fps, int bitrate) {
        native_init();
        videoChannel = new VideoChannel(this, activity, cameraId, width, height, fps, bitrate);
    }

    // 视频通道-->SurfaceView与中转站里面的Camera绑定
    public void setPreviewDisplay(SurfaceHolder holder) {
        videoChannel.setPreviewDisplay(holder);
    }

    // 视频通道-->切换摄像头
    public void switchCamera() {
        videoChannel.switchCamera();
    }

    /**
     * 开始直播
     * @param path rtmp地址
     */
    public void startLive(String path) {
        native_start(path);
        videoChannel.startLive();
        // audioChannel.startLive(); // 下节课都这样写
    }

    /**
     * 停止直播
     */
    public void stopLive() {
        videoChannel.stopLive();
        // audioChannel.stopLive(); // 下节课都这样写
        native_stop();
    }

    /**
     * 释放工作
     */
    public void release() {
        videoChannel.release();
        // audioChannel.release(); // 下节课都这样写
        native_release();
    }

    // 写很多的native函数 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

    // 音频 视频 公用的
    public native void native_init(); // 初始化
    public native void native_start(String path); // 开始直播start(音频视频通用一套代码) path:rtmp推流地址
    public native void native_stop(); // 停止直播
    public native void native_release(); // onDestroy--->release释放工作

    // 下面是视频独有
    public native void native_initVideoEncoder(int width, int height, int mFps, int bitrate); // 初始化x264编码器
    public native void native_pushVideo(byte[] data); // 相机画面的数据 byte[] 推给 C++层

}
