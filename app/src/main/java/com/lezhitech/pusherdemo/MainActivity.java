package com.lezhitech.pusherdemo;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.hardware.Camera;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.util.Log;
import android.view.SurfaceView;
import android.view.View;

public class MainActivity extends AppCompatActivity {
    static {
        System.loadLibrary("native-lib");
    }

    private static final String TAG="my_tag_MainActivity";
    private static final String[] permissions = new String[]{
            Manifest.permission.CAMERA,
            Manifest.permission.INTERNET,
            Manifest.permission.RECORD_AUDIO
    };
    private WePusher pusher;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_main);

        checkPermission();

        String tempVersion = stringFromJNI();
        Log.d(TAG, "onCreate: version=" + tempVersion);

        SurfaceView surfaceView = findViewById(R.id.surfaceView);

        /*cameraHelper = new CameraHelper(this, Camera.CameraInfo.CAMERA_FACING_FRONT, 640, 480);
        cameraHelper.setPreviewDisplay(surfaceView.getHolder());*/

        // 前置摄像头，宽，高，fps(每秒25帧)，码率/比特率：https://blog.51cto.com/u_7335580/2058648
//        pusher = new WePusher(this, Camera.CameraInfo.CAMERA_FACING_FRONT, 640, 480, 25, 800000);
        pusher = new WePusher(this, Camera.CameraInfo.CAMERA_FACING_BACK, 640, 480, 25, 800000);
        pusher.setPreviewDisplay(surfaceView.getHolder());

    }

    /**
     * 切换摄像头
     * @param view
     */
    public void switchCamera(View view) {
        pusher.switchCamera();
    }

    /**
     * 开始直播
     * @param view
     */
    public void startLive(View view) {
//        pusher.startLive("rtmp://139.224.136.101/myapp");
        pusher.startLive("rtmp://192.168.3.39:8899/live/"); // huya test 流媒体服务器分配的地址
    }

    /**
     * 停止直播
     * @param view
     */
    public void stopLive(View view) {
        pusher.stopLive();
    }

    /**
     * 释放工作
     */
    @Override
    protected void onDestroy() {
        super.onDestroy();
        pusher.release();
    }

    // WebRTC 直接视频预览

    public native String stringFromJNI();

    private void checkPermission() {
        Log.d(TAG,"checkPermission +");
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            for (String permission : permissions) {
                if (ContextCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
                    ActivityCompat.requestPermissions(this, permissions, 200);
                    return;
                }
            }
        }
        Log.d(TAG,"checkPermission -");
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && requestCode == 200) {
            for (int i = 0; i < permissions.length; i++) {
                if (grantResults[i] != PackageManager.PERMISSION_GRANTED) {
                    Intent intent = new Intent();
                    intent.setAction(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
                    Uri uri = Uri.fromParts("package", getPackageName(), null);
                    intent.setData(uri);
                    startActivityForResult(intent, 200);
                    return;
                }
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == 200 && resultCode == RESULT_OK) {
            checkPermission();
        }
    }
}