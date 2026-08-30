// LiveHaptics Qt GUI (Linux) - mirrors the Windows dark GUI
#include <QApplication>
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QVector>
#include <cstdio>
#include <cstring>
#include <string>
#include <hidapi/hidapi.h>
struct Meter : QWidget { int lvl=0; Meter(QWidget*p=nullptr):QWidget(p){setFixedHeight(16);}
  void paintEvent(QPaintEvent*){QPainter q(this);q.fillRect(rect(),QColor(58,58,64));
    q.fillRect(0,0,(int)((double)lvl/32768*width()),height(),QColor(80,200,120));}};
struct Preset{QString n;double g,b;int c;};
int main(int argc,char**argv){
  QApplication app(argc,argv);
  QProcess::execute("pkill", {"-x", "livehaptics"});
  QPalette pal;
  pal.setColor(QPalette::Window,QColor(30,30,32)); pal.setColor(QPalette::WindowText,QColor(225,225,225));
  pal.setColor(QPalette::Base,QColor(40,40,44));   pal.setColor(QPalette::AlternateBase,QColor(30,30,32));
  pal.setColor(QPalette::Text,QColor(225,225,225));pal.setColor(QPalette::Button,QColor(40,40,44));
  pal.setColor(QPalette::ButtonText,QColor(225,225,225));pal.setColor(QPalette::Highlight,QColor(0,120,215));
  pal.setColor(QPalette::HighlightedText,Qt::white);
  app.setPalette(pal);
  QString dir=QCoreApplication::applicationDirPath();
  QWidget win; win.setWindowTitle("LiveHaptics"); win.resize(1000,640);
  auto *root=new QHBoxLayout(&win);
  auto *left=new QVBoxLayout(); auto *list=new QListWidget();
  left->addWidget(new QLabel("Presets")); left->addWidget(list,1);
  auto *padd=new QPushButton("Add Preset"); auto *psave=new QPushButton("Save Selected"); auto *pdel=new QPushButton("Delete");
  left->addWidget(padd); left->addWidget(psave); left->addWidget(pdel);
  auto *mid=new QVBoxLayout();
  mid->addWidget(new QLabel("Haptics Settings"));
  mid->addWidget(new QLabel("Capture source:")); auto *ca=new QComboBox(); mid->addWidget(ca);
  mid->addWidget(new QLabel("Controller:")); auto *ch=new QComboBox(); mid->addWidget(ch);
  mid->addWidget(new QLabel("Gain:")); auto *g=new QSlider(Qt::Horizontal); g->setRange(0,50); g->setValue(20); mid->addWidget(g);
  mid->addWidget(new QLabel("Bass:")); auto *b=new QSlider(Qt::Horizontal); b->setRange(0,100); b->setValue(33); mid->addWidget(b);
  mid->addWidget(new QLabel("Latency cap:")); auto *c=new QSlider(Qt::Horizontal); c->setRange(0,32); c->setValue(16); mid->addWidget(c);
  auto *m16=new QCheckBox("Force 16-bit (wired)"); mid->addWidget(m16);
  auto *right=new QVBoxLayout();
  right->addWidget(new QLabel("Haptics Tester"));
  auto *conn=new QLabel("o Disconnected"); conn->setStyleSheet("color:#50c878"); right->addWidget(conn);
  right->addWidget(new QLabel("L")); auto *ml=new Meter(); right->addWidget(ml);
  right->addWidget(new QLabel("R")); auto *mr=new Meter(); right->addWidget(mr);
  auto *info=new QLabel(); right->addWidget(info);
  auto *start=new QPushButton("Start"); right->addWidget(start);
  auto *bar=new QLabel("Stopped - press Start"); right->addWidget(bar);
  root->addLayout(left,2); root->addLayout(mid,3); root->addLayout(right,3);
  QVector<Preset> presets{{"Music",2,0.17,16},{"Game",2.5,0.25,10},{"Movie",3,0.3,14}};
  { QFile f(dir+"/lh_presets.txt"); if(f.open(QFile::ReadOnly|QFile::Text)){ QTextStream t(&f); QString l;
      while(t.readLineInto(&l)){ auto p=l.split('\t'); if(p.size()==4) presets.push_back({p[0],p[1].toDouble(),p[2].toDouble(),p[3].toInt()}); } } }
  auto refresh=[&]{ list->clear(); for(auto&p:presets) list->addItem(p.n); };
  auto savePresets=[&]{ QFile f(dir+"/lh_presets.txt"); if(f.open(QFile::WriteOnly|QFile::Truncate|QFile::Text)){ QTextStream t(&f);
      for(auto&p:presets) t<<p.n<<'\t'<<p.g<<'\t'<<p.b<<'\t'<<p.c<<'\n'; } };
  refresh(); list->setCurrentRow(0);
  QProcess *proc=new QProcess(&win); bool want=false, gm=false,bm=false,cm=false, devm=false;
  auto popHid=[&]{ ch->clear(); hid_init(); auto *l=hid_enumerate(0x28de,0); int idx=0,first=-1;
    for(auto*e=l;e;e=e->next) if((e->product_id==0x1302||e->product_id==0x1304||e->product_id==0x1303)&&e->usage_page==0xFF00){
      bool ok=false; hid_device*d=hid_open_path(e->path); if(d){uint8_t pr[64]={0};pr[0]=0x88;ok=hid_write(d,pr,64)>=0;hid_close(d);}
      if(ok&&first<0)first=idx; ch->addItem(QString("Controller PID %1 #%2%3").arg(e->product_id,0,16).arg(idx++).arg(ok?"  <haptics>":"")); }
    hid_free_enumeration(l);
{ QFile cf(dir+"/livehaptics.cfg"); int sv=-1,sp=0;
  if(cf.open(QFile::ReadOnly)){ QTextStream ts(&cf); ts>>sv>>sp; }
  if(sv>=0&&sv<ch->count()&&ch->itemText(sv).contains(QString::number(sp,16))) ch->setCurrentIndex(sv);
  else if(first>=0) ch->setCurrentIndex(first); } ch->setCurrentIndex(first>=0?first:0); };
  auto popSrc=[&]{ ca->clear(); FILE*f=popen("pactl list short sources","r"); char line[512];
    while(f&&fgets(line,512,f)){ char nm[256]; if(sscanf(line,"%*d %255s",nm)==1){ QString q(nm); if(q.contains(".monitor")) ca->addItem(q);} } if(f)pclose(f); if(ca->count())ca->setCurrentIndex(0); };
  auto push=[&]{ if(proc->state()!=QProcess::Running)return; QFile f(dir+"/lh.cmd"); if(!f.open(QFile::WriteOnly|QFile::Truncate))return;
    QTextStream t(&f); if(gm)t<<"gain "<<g->value()/10.0<<'\n'; if(bm)t<<"bass "<<b->value()/100.0*0.5<<'\n'; if(cm)t<<"cap "<<c->value()<<'\n'; };
  auto startCore=[&]{ if(proc->state()!=QProcess::NotRunning)return;
    if(ca->count()) QProcess::execute("pactl",{"set-default-source",ca->currentText()});
    QStringList a; if(ch->count()>0&&ch->currentIndex()>=0)a<<"--dev"<<QString::number(ch->currentIndex());
    if(gm)a<<"--gain"<<QString::number(g->value()/10.0,'f',1); if(bm)a<<"--bass"<<QString::number(b->value()/100.0*0.5,'f',2);
    if(cm)a<<"--cap"<<QString::number(c->value()); if(m16->isChecked())a<<"--16bit";
    proc->setWorkingDirectory(dir); proc->start(dir+"/livehaptics",a); want=true; start->setText("Pause"); };
  auto stopCore=[&]{ want=false; if(proc->state()!=QProcess::NotRunning){proc->terminate();proc->waitForFinished(800);proc->terminate(); if (!proc->waitForFinished(2500)) proc->kill();} start->setText("Start"); };
  popHid(); popSrc();
  QObject::connect(ch,QOverload<int>::of(&QComboBox::currentIndexChanged),[&](int){devm=true;});
  QObject::connect(start,&QPushButton::clicked,[&]{ if(proc->state()!=QProcess::NotRunning){stopCore();conn->setText("o Disconnected");bar->setText("Stopped");} else { popHid(); devm=false; startCore(); } });
  QObject::connect(g,&QSlider::valueChanged,[&]{gm=true;push();});
  QObject::connect(b,&QSlider::valueChanged,[&]{bm=true;push();});
  QObject::connect(c,&QSlider::valueChanged,[&]{cm=true;push();});
  auto apply=[&](const Preset&p){ g->setValue((int)(p.g*10)); b->setValue((int)(p.b/0.5*100)); c->setValue(p.c); };
  QObject::connect(list,&QListWidget::currentRowChanged,[&](int r){ if(r>=0&&r<presets.size())apply(presets[r]); });
  QObject::connect(padd,&QPushButton::clicked,[&]{ presets.push_back({QString("Custom %1").arg(presets.size()+1),g->value()/10.0,b->value()/100.0*0.5,c->value()}); savePresets();refresh();list->setCurrentRow(presets.size()-1); });
  QObject::connect(psave,&QPushButton::clicked,[&]{ int r=list->currentRow(); if(r>=0){presets[r]={presets[r].n,g->value()/10.0,b->value()/100.0*0.5,c->value()};savePresets();} });
  QObject::connect(pdel,&QPushButton::clicked,[&]{ int r=list->currentRow(); if(r>=0&&presets.size()>1){presets.remove(r);savePresets();refresh();list->setCurrentRow(0);} });
  auto *tm=new QTimer(&win);
  QObject::connect(tm,&QTimer::timeout,[&]{
    if(want&&proc->state()==QProcess::NotRunning){conn->setText("o Reconnecting");popHid();startCore();}
    QFile f(dir+"/lh.stat");
    if(f.open(QFile::ReadOnly)){ auto p=QString(f.readAll()).simplified().split(' ');
      if(p.size()>=5){ ml->lvl=p[0].toInt(); mr->lvl=p[1].toInt();
        conn->setText("* Connected"); info->setText(QString("PID %1\n%2 packets").arg(p[4]).arg(p[2]));
        bar->setText(QString("Streaming active - %1 - PID %2").arg(p[3]).arg(p[4]));
        ml->update(); mr->update(); } }
    else if(!want){conn->setText("o Disconnected");} });
  tm->start(200);
  win.show();
  { int r = app.exec(); proc->terminate(); proc->waitForFinished(500); proc->terminate(); if (!proc->waitForFinished(2500)) proc->kill(); return r; }
}
