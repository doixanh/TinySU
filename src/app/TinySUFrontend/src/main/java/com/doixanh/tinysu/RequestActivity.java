package com.doixanh.tinysu;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.widget.TextView;

import com.doixanh.tinysu.R;

public class RequestActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_request);

        Intent intent = getIntent();
        int uid = intent.getIntExtra("uid", 0);
        String names[] = getPackageManager().getPackagesForUid(uid);

        String all = "";
        PackageManager packageManager= getApplicationContext().getPackageManager();
        try {
            String appName = (String) packageManager.getApplicationLabel(packageManager.getApplicationInfo(names[0], PackageManager.GET_META_DATA));
            all += appName + "\n\n";
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
        }

        for (String name: names) {
            all += name + "\n";
        }
        TextView tv = (TextView) findViewById(R.id.uid);
        tv.setText(all);
    }
}