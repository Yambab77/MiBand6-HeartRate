# MiBand6-HeartRate
使用C++开发，通过启用小米手环6自带的运动心率广播功能，在Windows主机上接收其蓝牙广播数据，并将实时心率数据显示在屏幕上。

增加在网站实时显示广播心率的功能

使用WordPress 插件 WP REST API接收客户端的数据（WordPress 4.7+ 已内置）。
需要在网站添加如下代码
步骤一：前端 AJAX 轮询
在首页页脚或自定义 JS 里加如下代码（每秒刷新一次）：
//
<script>
function updateHR() {
    fetch('https://www.woyoudu.cn/wp-json/hr/v1/current/')
        .then(r => r.json())
        .then(d => {
            document.getElementById('hr-value').innerText = d.hr || '--';
        });
}
setInterval(updateHR, 1000);
window.onload = updateHR;
</script>
<span>当前心率：<span id="hr-value">--</span></span>


步骤二：检查 REST API 注册代码
请确保你在主题的 functions.php 或自定义插件中完整添加了如下代码：

//
add_action('rest_api_init', function () {
<script>
const FALLBACK_TEXT = "未获取到数据";

function updateHR() {
  fetch('/wp-json/hr/v1/current/')
    .then(r => r.json())
    .then(d => {
      const el = document.getElementById('hr-value');
      if (!el) return;
      if (d.stale || !d.hr) {
        el.innerText = FALLBACK_TEXT;
      } else {
        el.innerText = d.hr;
      }
    })
    .catch(() => {
      const el = document.getElementById('hr-value');
      if (el) el.innerText = FALLBACK_TEXT;
    });
}
setInterval(updateHR, 2000);
window.addEventListener('load', updateHR);
</script>

<span>当前心率：<span id="hr-value">--</span></span>

    // 获取当前心率：返回 hr、时间戳、是否过期
    register_rest_route('hr/v1', '/current/', array(
        'methods'  => 'GET',
        'callback' => function () {
            $payload = get_transient('current_hr_payload');
            if (!is_array($payload)) {
                $payload = array('hr' => 0, 'ts' => 0);
            }
            $age = time() - intval($payload['ts']);
            $stale = ($age > 300); // 超过 5 分钟判为过期
            return array(
                'hr'          => intval($payload['hr']),
                'ageSeconds'  => max(0, $age),
                'stale'       => $stale,
            );
        },
        'permission_callback' => '__return_true',
    ));
});
