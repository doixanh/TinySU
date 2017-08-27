package com.doixanh.tinysu;

import android.app.Activity;
import android.content.Intent;
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
        for (String name: names) {
            all += name + "\n";
        }
        TextView tv = (TextView) findViewById(R.id.uid);
        tv.setText("Requesting root permission \n" + all);
    }
}