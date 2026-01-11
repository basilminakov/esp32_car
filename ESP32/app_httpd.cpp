#include "esp_http_server.h"
#include "esp_camera.h"
#include "Arduino.h"

extern int gpLed;
extern int gpRf;
extern int gpRb;
extern int gpLf;
extern int gpLb;
extern String WiFiAddr;
byte txdata[3] = { 0xA5, 0, 0x5A };
const int Forward = 92;
const int Backward = 163;
const int Turn_Left = 149;
const int Turn_Right = 106;
const int Top_Left = 20;
const int Bottom_Left = 129;
const int Top_Right = 72;
const int Bottom_Right = 34;
const int Stop = 0;
const int Clockwise = 83;
const int Counterclockwise = 172;
const int Moedl1 = 25;
const int Moedl2 = 26;
const int Moedl3 = 27;
const int Moedl4 = 28;
const int MotorLeft = 230;
const int MotorRight = 231;
const int MotorCenter = 232;

#define HTTPD_CONFIG_ESP32_CAR() \
  { \
    .task_priority = tskIDLE_PRIORITY + 5, \
    .stack_size = 8192, \
    .core_id = tskNO_AFFINITY, \
    .task_caps = (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), \
    .server_port = 80, \
    .ctrl_port = ESP_HTTPD_DEF_CTRL_PORT, \
    .max_open_sockets = 7, \
    .max_uri_handlers = 21, \
    .max_resp_headers = 21, \
    .backlog_conn = 5, \
    .lru_purge_enable = false, \
    .recv_wait_timeout = 5, \
    .send_wait_timeout = 5, \
    .global_user_ctx = NULL, \
    .global_user_ctx_free_fn = NULL, \
    .global_transport_ctx = NULL, \
    .global_transport_ctx_free_fn = NULL, \
    .enable_so_linger = false, \
    .linger_timeout = 0, \
    .keep_alive_enable = false, \
    .keep_alive_idle = 0, \
    .keep_alive_interval = 0, \
    .keep_alive_count = 0, \
    .open_fn = NULL, \
    .close_fn = NULL, \
    .uri_match_fn = NULL \
  }

typedef struct
{
  size_t size;   // number of values used for filtering
  size_t index;  // current value index
  size_t count;  // value count
  int sum;
  int *values;  // array to be filled with values
} ra_filter_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static ra_filter_t ra_filter;
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size) {
  memset(filter, 0, sizeof(ra_filter_t));

  filter->values = (int *)malloc(sample_size * sizeof(int));
  if (!filter->values) {
    return NULL;
  }
  memset(filter->values, 0, sample_size * sizeof(int));

  filter->size = sample_size;
  return filter;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[64];

  static int64_t last_frame = 0;
  if (!last_frame) {
    last_frame = esp_timer_get_time();
  }

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.printf("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          Serial.printf("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      break;
    }
    int64_t fr_end = esp_timer_get_time();

    int64_t frame_time = fr_end - last_frame;
    last_frame = fr_end;
    frame_time /= 1000;
  }

  last_frame = 0;
  return res;
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  String page = "";
  page += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=0\">\n";
  page += "<script>var xhttp = new XMLHttpRequest();</script>\n";
  page += "<script>function getsend(arg) { xhttp.open('GET', arg +'?' + new Date().getTime(), true); xhttp.send() } </script>\n";
  page += "<style>\n";
  page += ".ctrl_btn {\n";
  page += "    width: 120px;\n";
  page += "    height: 40px;\n";
  page += "    background-color: slategray;\n";
  page += "    border-radius: 8px;\n";
  page += "    margin: 5px;\n";
  page += "}\n";
  page += ".stop { background-color: indianred; }\n";
  page += ".light { background-color: yellow; }\n";
  page += "</style>\n";
  page += "<body>\n";
  page += "<p align=center><IMG SRC='http://192.168.4.1:81/stream' style='width:320px;transform:rotate(180deg);'></p>\n";
  page += "<div align=center>\n";
  page += "<button class=\"ctrl_btn\" onmousedown=getsend('leftup') onmouseup=getsend('stop') ontouchstart=getsend('leftup') ontouchend=getsend('stop')><b>Влево вперёд</b></button>\n";
  page += "<button class=\"ctrl_btn\" onmousedown=getsend('go') onmouseup=getsend('stop') ontouchstart=getsend('go') ontouchend=getsend('stop') ><b>Вперёд</b></button>\n";
  page += "<button class=\"ctrl_btn\" onmousedown=getsend('rightup') onmouseup=getsend('stop') ontouchstart=getsend('rightup') ontouchend=getsend('stop') ><b>Вправо вперёд</b></button>\n";
  page += "</div>\n\n";
  page += "<div align=center>\n";
  page += "<button class=\"ctrl_btn\" onmousedown=getsend('left') onmouseup=getsend('stop') ontouchstart=getsend('left') ontouchend=getsend('stop')><b>Влево</b></button>\n";
  page += "<button class=\"ctrl_btn stop\" onmousedown=getsend('stop') onmouseup=getsend('stop')><b>Стоп</b></button>\n";
  page += "<button class=\"ctrl_btn\" onmousedown=getsend('right') onmouseup=getsend('stop') ontouchstart=getsend('right') ontouchend=getsend('stop')><b>Вправо</b></button>\n";
  page += "</div>\n\n";
  page += "<div align=center>\n";
  page += "<button class=\"ctrl_btn\" onmousedown=getsend('leftdown') onmouseup=getsend('stop') ontouchstart=getsend('leftdown') ontouchend=getsend('stop') ><b>Влево назад</b></button>\n";
  page += "<button class=\"ctrl_btn\" onmousedown=getsend('back') onmouseup=getsend('stop') ontouchstart=getsend('back') ontouchend=getsend('stop') ><b>Назад</b></button>\n";
  page += "<button class=\"ctrl_btn\" onmousedown=getsend('rightdown') onmouseup=getsend('stop') ontouchstart=getsend('rightdown') ontouchend=getsend('stop') ><b>Вправо назад</b></button>\n";
  page += "</div>\n\n";
  page += "<div align=center>\n";
  page += "<button class=\"ctrl_btn\" onmousedown=getsend('counterclockwise') onmouseup=getsend('stop') ontouchstart=getsend('counterclockwise') ontouchend=getsend('stop')><b>Разворот влево</b></button>\n";
  page += "<button class=\"ctrl_btn\" onmousedown=getsend('clockwise') onmouseup=getsend('stop') ontouchstart=getsend('clockwise') ontouchend=getsend('stop')><b>Разворот вправо</b></button>\n";
  page += "</div>\n\n\n";
  page += "<div align=center>\n";
  page += "<button class=\"ctrl_btn light\" onmousedown=getsend('ledon')><b>Вкл. свет</b></button>\n";
  page += "<button class=\"ctrl_btn light\" onmousedown=getsend('ledoff')><b>Выкл. свет</b></button>\n";
  page += "</div>\n";
  page += "</body>";
  return httpd_resp_send(req, &page[0], strlen(&page[0]));
}

static void WheelAct(int nLf, int nLb, int nRf, int nRb)
{
 digitalWrite(gpLf, nLf);
 digitalWrite(gpLb, nLb);
 digitalWrite(gpRf, nRf);
 digitalWrite(gpRb, nRb);
}

static esp_err_t go_handler(httpd_req_t *req) {
  // txdata[1] = Forward;
  // Serial.write(txdata, 3);
  WheelAct(HIGH, LOW, HIGH, LOW);
  Serial.println("Go");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t back_handler(httpd_req_t *req) {
  // txdata[1] = Backward;
  // Serial.write(txdata, 3);
  WheelAct(LOW, HIGH, LOW, HIGH);
  Serial.println("Back");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t left_handler(httpd_req_t *req) {
  // txdata[1] = Turn_Left;
  // Serial.write(txdata, 3);
  WheelAct(HIGH, LOW, LOW, HIGH);
  Serial.println("Left");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t right_handler(httpd_req_t *req) {
  // txdata[1] = Turn_Right;
  // Serial.write(txdata, 3);
  WheelAct(LOW, HIGH, HIGH, LOW);
  Serial.println("Right");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t stop_handler(httpd_req_t *req) {
  // txdata[1] = Stop;
  // Serial.write(txdata, 3);
  WheelAct(LOW, LOW, LOW, LOW);
  Serial.println("Stop");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t ledon_handler(httpd_req_t *req) {
  digitalWrite(gpLed, HIGH);
  Serial.println("LED ON");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t ledoff_handler(httpd_req_t *req) {
  digitalWrite(gpLed, LOW);
  Serial.println("LED OFF");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t leftup_handler(httpd_req_t *req) {
  txdata[1] = Top_Left;
  Serial.write(txdata, 3);
  Serial.println("leftup");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t leftdown_handler(httpd_req_t *req) {
  txdata[1] = Bottom_Left;
  Serial.write(txdata, 3);
  Serial.println("leftdown");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t rightup_handler(httpd_req_t *req) {
  txdata[1] = Top_Right;
  Serial.write(txdata, 3);
  Serial.println("rightup");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t rightdown_handler(httpd_req_t *req) {
  txdata[1] = Bottom_Right;
  Serial.write(txdata, 3);
  Serial.println("rightdown");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t counterclockwise_handler(httpd_req_t *req) {
  txdata[1] = Counterclockwise;
  Serial.write(txdata, 3);
  Serial.println("counterclockwise");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t clockwise_handler(httpd_req_t *req) {
  txdata[1] = Clockwise;
  Serial.write(txdata, 3);
  Serial.println("clockwise");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t model1_handler(httpd_req_t *req) {
  txdata[1] = Moedl1;
  Serial.write(txdata, 3);
  Serial.println("model1");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t model2_handler(httpd_req_t *req) {
  txdata[1] = Moedl2;
  Serial.write(txdata, 3);
  Serial.println("model2");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t model3_handler(httpd_req_t *req) {
  txdata[1] = Moedl3;
  Serial.write(txdata, 3);
  Serial.println("model3");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t model4_handler(httpd_req_t *req) {
  txdata[1] = Moedl4;
  Serial.write(txdata, 3);
  Serial.println("model4");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t motorleft_handler(httpd_req_t *req) {
  txdata[1] = MotorLeft;
  Serial.write(txdata, 3);
  Serial.println("MotorLeft");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t motorright_handler(httpd_req_t *req) {
  txdata[1] = MotorRight;
  Serial.write(txdata, 3);
  Serial.println("MotorRight");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}
static esp_err_t motorcenter_handler(httpd_req_t *req) {
  txdata[1] = MotorCenter;
  Serial.write(txdata, 3);
  Serial.println("MotorCenter");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, "OK", 2);
}

void startCameraServer() {
  httpd_config_t config = HTTPD_CONFIG_ESP32_CAR();

  httpd_uri_t go_uri = {
    .uri = "/go",
    .method = HTTP_GET,
    .handler = go_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t back_uri = {
    .uri = "/back",
    .method = HTTP_GET,
    .handler = back_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t stop_uri = {
    .uri = "/stop",
    .method = HTTP_GET,
    .handler = stop_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t left_uri = {
    .uri = "/left",
    .method = HTTP_GET,
    .handler = left_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t right_uri = {
    .uri = "/right",
    .method = HTTP_GET,
    .handler = right_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t leftup_uri = {
    .uri = "/leftup",
    .method = HTTP_GET,
    .handler = leftup_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t leftdown_uri = {
    .uri = "/leftdown",
    .method = HTTP_GET,
    .handler = leftdown_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t rightup_uri = {
    .uri = "/rightup",
    .method = HTTP_GET,
    .handler = rightup_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t rightdown_uri = {
    .uri = "/rightdown",
    .method = HTTP_GET,
    .handler = rightdown_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t counterclockwise_uri = {
    .uri = "/counterclockwise",
    .method = HTTP_GET,
    .handler = counterclockwise_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t clockwise_uri = {
    .uri = "/clockwise",
    .method = HTTP_GET,
    .handler = clockwise_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t model1_uri = {
    .uri = "/model1",
    .method = HTTP_GET,
    .handler = model1_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t model2_uri = {
    .uri = "/model2",
    .method = HTTP_GET,
    .handler = model2_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t model3_uri = {
    .uri = "/model3",
    .method = HTTP_GET,
    .handler = model3_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t model4_uri = {
    .uri = "/model4",
    .method = HTTP_GET,
    .handler = model4_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t motorleft_uri = {
    .uri = "/motorleft",
    .method = HTTP_GET,
    .handler = motorleft_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t motorright_uri = {
    .uri = "/motorright",
    .method = HTTP_GET,
    .handler = motorright_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t motorcenter_uri = {
    .uri = "/motorcenter",
    .method = HTTP_GET,
    .handler = motorcenter_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t ledon_uri = {
    .uri = "/ledon",
    .method = HTTP_GET,
    .handler = ledon_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t ledoff_uri = {
    .uri = "/ledoff",
    .method = HTTP_GET,
    .handler = ledoff_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  ra_filter_init(&ra_filter, 20);
  Serial.printf("Starting web server on port: '%d'", config.server_port);

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &go_uri);
    httpd_register_uri_handler(camera_httpd, &back_uri);
    httpd_register_uri_handler(camera_httpd, &stop_uri);
    httpd_register_uri_handler(camera_httpd, &left_uri);
    httpd_register_uri_handler(camera_httpd, &right_uri);
    httpd_register_uri_handler(camera_httpd, &ledon_uri);
    httpd_register_uri_handler(camera_httpd, &ledoff_uri);
    httpd_register_uri_handler(camera_httpd, &leftup_uri);
    httpd_register_uri_handler(camera_httpd, &leftdown_uri);
    httpd_register_uri_handler(camera_httpd, &rightup_uri);
    httpd_register_uri_handler(camera_httpd, &rightdown_uri);
    httpd_register_uri_handler(camera_httpd, &counterclockwise_uri);
    httpd_register_uri_handler(camera_httpd, &clockwise_uri);
    httpd_register_uri_handler(camera_httpd, &model1_uri);
    httpd_register_uri_handler(camera_httpd, &model2_uri);
    httpd_register_uri_handler(camera_httpd, &model3_uri);
    httpd_register_uri_handler(camera_httpd, &model4_uri);
    httpd_register_uri_handler(camera_httpd, &motorleft_uri);
    httpd_register_uri_handler(camera_httpd, &motorright_uri);
    httpd_register_uri_handler(camera_httpd, &motorcenter_uri);
  }

  config.server_port += 1;
  config.ctrl_port += 1;
  Serial.printf("Starting stream server on port: '%d'", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}
