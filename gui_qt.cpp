// LiveHaptics Qt GUI v1.2 - UAI-style dark shell (Linux)
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QProgressBar>
#include <hidapi/hidapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
static QComboBox *ch, *rate, *capc;
static QSlider *g, *b, *c;
static QLabel *vg, *vb, *vc, *conn, *mode, *info, *bar;
static QCheckBox *m16;
static QPushButton *start;
static QProgressBar *ml, *mr;
static QProcess *proc = nullptr;
static bool want = false;
static double SldG() { return g->value() / 10.0; }
static double SldB() { return b->value() / 100.0; }
static int SldC() { return c->value(); }
static void labels() {
  vg->setText(QString::number(SldG(), 'f', 1));
  vb->setText(QString::number(SldB(), 'f', 2));
  vc->setText(QString::number(SldC()));
}
static void push() {
  labels();
  if (!proc || proc->state() != QProcess::Running) return;
  QFile t("lh.cmd");
  if (t.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QTextStream s(&t);
    s << "gain " << SldG() << "\nbass " << SldB() << "\ncap " << SldC() << "\n";
  }
}
static void populateCap() {
  capc->clear();
  QProcess p;
  p.start("pactl", QStringList() << "list" << "short" << "sources");
  p.waitForFinished(3000);
  for (const QByteArray &ln : p.readAllStandardOutput().split('\n')) {
    QStringList f = QString::fromUtf8(ln).split('\t');
    if (f.size() >= 2 && !f[1].trimmed().isEmpty()) capc->addItem(f[1]);
  }
  if (capc->count() == 0) capc->addItem("");
}
static void populateHid() {
  QString keep = ch->currentText();
  ch->clear();
  hid_init();
  hid_device_info *l = hid_enumerate(0x28de, 0);
  int idx = 0, first_ok = -1, want_i = -1;
  for (auto *e = l; e; e = e->next)
    if ((e->product_id == 0x1302 || e->product_id == 0x1304 || e->product_id == 0x1303) && e->usage_page == 0xFF00) {
      bool ok = false; hid_device *d = hid_open_path(e->path);
      if (d) { uint8_t pr[64] = {0}; pr[0] = 0x88; ok = hid_write(d, pr, 64) >= 0; hid_close(d); }
      if (ok && first_ok < 0) first_ok = idx;
      char buf[96]; snprintf(buf, 96, "Controller PID %04x #%d%s", e->product_id, idx++, ok ? "  <haptics>" : "");
      ch->addItem(buf);
      if (keep == buf) want_i = ch->count() - 1;
    }
  hid_free_enumeration(l);
  ch->setCurrentIndex(want_i >= 0 ? want_i : (first_ok >= 0 ? first_ok : 0));
}
static void stopCore() {
  want = false;
  if (proc && proc->state() != QProcess::NotRunning) {
    QFile t("lh.cmd");
    if (t.open(QIODevice::WriteOnly | QIODevice::Truncate)) { t.write("stop\n"); t.close(); }
    proc->terminate();
    if (!proc->waitForFinished(2000)) proc->kill();
  }
  start->setText("Start");
}
static void startCore() {
  if (proc && proc->state() != QProcess::NotRunning) return;
  QFile::remove("lh.cmd");
  QStringList a;
  a << QString("--gain %1").arg(SldG(), 0, 'f', 1)
    << QString("--bass %1").arg(SldB(), 0, 'f', 2)
    << QString("--cap %1").arg(SldC());
  if (m16->isChecked()) a << "--16bit";
  int ci = ch->currentIndex();
  QString txt = ch->currentText();
  if (ci >= 0) a << "--dev" << QString::number(ci);
  bool wired = txt.contains("1302");
  int ri = rate->currentIndex();
  if (!wired && ri == 1) a << "--rate" << "4000";
  if (!wired && ri == 2) a << "--rate" << "8000";
  if (capc->currentIndex() >= 0 && !capc->currentText().isEmpty()) {
    a << "--src" << capc->currentText();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PULSE_SOURCE", capc->currentText());
    proc->setProcessEnvironment(env);
  }
  proc->setWorkingDirectory(QDir::currentPath());
  proc->start(QDir::currentPath() + "/livehaptics", a);
  want = true;
  start->setText("Pause");
}
static void tick() {
  if (want && proc && proc->state() == QProcess::NotRunning) {
    populateHid(); startCore();
  }
  QFile t("lh.stat");
  if (t.open(QIODevice::ReadOnly)) {
    QByteArray d = t.readAll();
    int l = 0, r = 0; unsigned long long s = 0; char mo[16] = {0}; unsigned pid = 0;
    if (sscanf(d.constData(), "%d %d %llu %15s %x", &l, &r, &s, mo, &pid) == 5) {
      ml->setValue(l); mr->setValue(r);
      conn->setText("* Connected"); conn->setStyleSheet("color:#6ccb5f;");
      mode->setText(mo);
      info->setText(QString("PID %1  -  %2 packets").arg(pid, 4, 16, QChar('0')).arg(s));
      bar->setText(QString("Streaming active - %1 - PID %2").arg(mo).arg(pid, 4, 16, QChar('0')));
    }
  } else if (!want) { conn->setText("o Disconnected"); conn->setStyleSheet("color:#9a9a9a;"); }
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setStyleSheet(
    "QWidget{background:#1f1f1f;color:#e1e1e1;} "
    "QGroupBox{border:1px solid #3a3a3a;border-radius:7px;margin-top:12px;padding-top:6px;} "
    "QGroupBox::title{subcontrol-origin:margin;left:12px;color:#9a9a9a;} "
    "QComboBox{background:#2d2d2d;border:1px solid #3a3a3a;border-radius:4px;padding:3px 8px;min-height:22px;} "
    "QComboBox QAbstractItemView{background:#2d2d2d;selection-background-color:#0078d4;} "
    "QPushButton{background:#2d2d2d;border:1px solid #3a3a3a;border-radius:4px;padding:5px 14px;} "
    "QPushButton:hover{background:#3a3a3a;} "
    "QPushButton#startBtn{background:#0078d4;color:#ffffff;} "
    "QPushButton#startBtn:hover{background:#0090f0;} "
    "QCheckBox{spacing:6px;} "
    "QProgressBar{background:#3a3a3a;border:none;border-radius:3px;min-height:18px;} "
    "QProgressBar::chunk{background:#4cc2ff;} "
    "QSlider::groove:horizontal{height:4px;background:#3a3a3a;border-radius:2px;} "
    "QSlider::handle:horizontal{width:14px;height:14px;margin:-5px 0;background:#4cc2ff;border-radius:7px;} "
    "QSlider::sub-page:horizontal{background:#0078d4;border-radius:2px;} "
    "QLabel{background:transparent;}");
  QWidget w;
  QVBoxLayout *root = new QVBoxLayout(&w);
  QHBoxLayout *hdr = new QHBoxLayout();
  QLabel *title = new QLabel("LiveHaptics");
  title->setStyleSheet("font-size:22px;font-weight:600;");
  mode = new QLabel(""); mode->setStyleSheet("color:#4cc2ff;");
  conn = new QLabel("o Disconnected"); conn->setStyleSheet("color:#9a9a9a;");
  start = new QPushButton("Start"); start->setObjectName("startBtn");
  hdr->addWidget(title); hdr->addStretch(); hdr->addWidget(mode); hdr->addWidget(conn); hdr->addWidget(start);
  root->addLayout(hdr);
  QHBoxLayout *cols = new QHBoxLayout();
  QGroupBox *set = new QGroupBox("Haptics Settings");
  QVBoxLayout *sv = new QVBoxLayout(set);
  sv->addWidget(new QLabel("Capture source"));
  capc = new QComboBox(); sv->addWidget(capc);
  sv->addWidget(new QLabel("Controller"));
  ch = new QComboBox(); sv->addWidget(ch);
  sv->addWidget(new QLabel("Wireless rate"));
  rate = new QComboBox();
  rate->addItem("Auto (recommended)");
  rate->addItem("4 kHz - clean wireless");
  rate->addItem("8 kHz - hi-fi (may pop)");
  sv->addWidget(rate);
  auto row = [&](const char *n, QSlider *s, QLabel *&v, int max, int d) {
    QHBoxLayout *h = new QHBoxLayout();
    h->addWidget(new QLabel(n));
    s->setRange(0, max); s->setValue(d);
    h->addWidget(s);
    v = new QLabel(); h->addWidget(v);
    sv->addLayout(h);
  };
  row("Gain", g = new QSlider(Qt::Horizontal), vg, 50, 20);
  row("Bass", b = new QSlider(Qt::Horizontal), vb, 100, 17);
  row("Latency cap", c = new QSlider(Qt::Horizontal), vc, 32, 16);
  m16 = new QCheckBox("Force 16-bit (wired)"); sv->addWidget(m16);
  sv->addStretch();
  QGroupBox *tst = new QGroupBox("Haptics Tester");
  QVBoxLayout *tv = new QVBoxLayout(tst);
  ml = new QProgressBar(); ml->setRange(0, 32767); ml->setTextVisible(false);
  mr = new QProgressBar(); mr->setRange(0, 32767); mr->setTextVisible(false);
  tv->addWidget(new QLabel("L")); tv->addWidget(ml);
  tv->addWidget(new QLabel("R")); tv->addWidget(mr);
  info = new QLabel(""); tv->addWidget(info);
  tv->addStretch();
  cols->addWidget(set, 3); cols->addWidget(tst, 2);
  root->addLayout(cols);
  bar = new QLabel("Stopped - press Start"); root->addWidget(bar);
  w.setWindowTitle("LiveHaptics");
  w.resize(860, 560);
  w.show();
  proc = new QProcess(&w);
  populateCap(); populateHid(); labels();
  QObject::connect(start, &QPushButton::clicked, [] {
    if (proc && proc->state() != QProcess::NotRunning) { stopCore(); bar->setText("Stopped"); }
    else { populateHid(); startCore(); }
  });
  QObject::connect(g, &QSlider::valueChanged, [](int) { push(); });
  QObject::connect(b, &QSlider::valueChanged, [](int) { push(); });
  QObject::connect(c, &QSlider::valueChanged, [](int) { push(); });
  QObject::connect(capc, QOverload<int>::of(&QComboBox::currentIndexChanged), [](int) {
    if (proc && proc->state() != QProcess::NotRunning) { stopCore(); startCore(); }
  });
  QTimer *t = new QTimer(&w);
  QObject::connect(t, &QTimer::timeout, tick);
  t->start(200);
  return app.exec();
}
