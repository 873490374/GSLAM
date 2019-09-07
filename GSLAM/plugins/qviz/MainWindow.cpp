#ifdef HAS_QT
#include <QKeyEvent>
#include <QMessageBox>
#include <QSplitter>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>

#include <GSLAM/core/Messenger.h>
#include <GSLAM/core/Evaluation.h>

#include "MainWindow.h"

using namespace std;

namespace GSLAM{

enum ThreadStatus
{
    READY,RUNNING,PAUSE,STOP,ONESTEP
};

class SvarQTreeItem:public QTreeWidgetItem
{
public:
    SvarQTreeItem(const string& cmd,QTreeWidgetItem* parent,const QString &strings,int type,int def)
        : QTreeWidgetItem(parent,QStringList() <<strings,type),var(svar.GetInt(cmd,def))
    {
        items().set<SvarQTreeItem*>(cmd,this);

        if(parent)
            parent->addChild(this);
//        setSelected(true);

        if(!svar.GetInt(cmd,def))
        {
            setCheckState(0,Qt::Unchecked);
        }
        else if(svar.GetInt(cmd,def)==1)
            setCheckState(0,Qt::PartiallyChecked);
        else
        {
            setCheckState(0,Qt::Checked);
//            if(parent)
//            {
//                if(parent->checkState(0)==Qt::Unchecked)
//                    parent->setCheckState(0,Qt::Checked);
//            }
        }

        setToolTip(0,cmd.c_str());
    }

    static SvarQTreeItem* get(const string& cmd){return items().Get<SvarQTreeItem*>(cmd,nullptr);}

    static Svar& items(){
        static Svar var;
        return var;
    }


    int& var;
};

ShowLayerWidget::ShowLayerWidget(QWidget* parent):QTreeWidget(parent){
    using namespace std::placeholders;
    svar.def("AddLayer",[this](std::string itemName){addItem(itemName,2);});
    //    this->setTabKeyNavigation(false);
    setHeaderLabel(tr("Layers"));
    setHeaderHidden(true);
    SvarQTreeItem* rootTree=new SvarQTreeItem("Root",NULL,tr("Root"),0,2);
    addTopLevelItem(rootTree);
    connect(this,SIGNAL(itemChanged(QTreeWidgetItem*,int)),
            this,SLOT(changedSlot(QTreeWidgetItem*,int)));
    connect(this,SIGNAL(signalAddItem(QString,int)),this,SLOT(slotAddItem(QString,int)));
}

void ShowLayerWidget::changedSlot(QTreeWidgetItem *item, int column)
{
    SvarQTreeItem* itemSvar=(SvarQTreeItem*)item;
    if(itemSvar->var!=item->checkState(column))
    {
        itemSvar->var=item->checkState(column);
        svar.call("LayerUpdate",item->toolTip(0).toStdString());
        svar.call("MainWindow.Update");
        emit signalStatusChanged(item->toolTip(0),item->checkState(column));
    }
}

bool ShowLayerWidget::changeLanguage()
{
    return false;
}

void ShowLayerWidget::addItem(std::string itemName,int status)
{
    emit signalAddItem(itemName.c_str(),status);
}

void ShowLayerWidget::slotAddItem(QString itemName,int status)
{
    std::string itemStr=itemName.toStdString();
    if(!itemStr.empty()){
        auto dotIdx=itemStr.find_last_of('.');
        if(dotIdx==std::string::npos){
            SvarQTreeItem* item=SvarQTreeItem::get(itemStr);
            if(!item)
            {
                item=new SvarQTreeItem(itemStr.c_str(),SvarQTreeItem::get("Root"),itemStr.c_str(),0,status);
            }
            return ;
        }

        std::string parent=itemStr.substr(0,dotIdx);
        std::string name=itemStr.substr(dotIdx+1);
        SvarQTreeItem* itemP=SvarQTreeItem::get(parent);
        if(!itemP) slotAddItem(parent.c_str(),status);
        itemP=SvarQTreeItem::get(parent);
        itemP=new SvarQTreeItem(itemStr,itemP,name.c_str(),0,status);
    }
}

////////////////////////////////////////////////////////////////////////////////
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),frameVis(NULL),status(READY),historyFile("history.txt"),
      defaultSLAMs(svar.Get<VecParament<std::string> >("SLAM",VecParament<std::string>())),
      defaultDataset(svar.GetString("Dataset",""))
{
    // set window minimum size
    this->setMinimumSize(1366, 700);

    // window title
    setWindowTitle("GSLAM");

    svar.def("MainWindow.Show",[this](){call("Show");});
    svar.def("MainWindow.Update",[this](){call("Update");});
    svar.def("MainWindow.SetRadius",[](){});
    // setup layout
    setupLayout();
    connect(this, SIGNAL(call_signal(QString) ),
            this, SLOT(call_slot(QString)) );
    connect(this,SIGNAL(signalDatasetStatusUpdated(int)),
            this,SLOT(slotDatasetStatusUpdated(int)));
    connect(this,SIGNAL(signalTryVisualize(QString,QString)),
            this,SLOT(slotTryVisualize(QString,QString)));
}

int MainWindow::setupLayout(void)
{
    toolBar=addToolBar(tr("&ToolBar"));
    fileMenu    =menuBar()->addMenu(tr("&File"));
    runMenu     =menuBar()->addMenu(tr("&Run"));

    exportMenu  =new QMenu(tr("&Export"),fileMenu);
    historyMenu =new QMenu(tr("&History"),fileMenu);
    openAction  =new QAction(tr("&Open"),fileMenu);
    startAction =new QAction(tr("&Start"),runMenu);
    pauseAction =new QAction(tr("&Pause"),runMenu);
    stopAction  =new QAction(tr("&Stop"),runMenu);
    oneStepAction=new QAction(tr("&OneStep"),runMenu);

    fileMenu->addAction(openAction);
    fileMenu->addMenu(exportMenu);
    fileMenu->addMenu(historyMenu);
    runMenu->addAction(startAction);
    runMenu->addAction(pauseAction);
    runMenu->addAction(stopAction);
    runMenu->addAction(oneStepAction);

    toolBar->setMovable(true);
    toolBar->addAction(openAction);
    toolBar->addSeparator();
    toolBar->addAction(startAction);
    toolBar->addAction(pauseAction);
    toolBar->addAction(oneStepAction);
    toolBar->addAction(stopAction);
    pauseAction->setDisabled(true);
    stopAction->setDisabled(true);
    startAction->setDisabled(true);
    oneStepAction->setDisabled(true);

    QString iconFolder=":icon";
    openAction->setIcon(QIcon(iconFolder+"/open.png"));
    exportMenu->setIcon(QIcon(iconFolder+"/export.png"));
    startAction->setIcon(QIcon(iconFolder+"/start.png"));
    pauseAction->setIcon(QIcon(iconFolder+"/pause.png"));
    stopAction->setIcon(QIcon(iconFolder+"/stop.png"));
    oneStepAction->setIcon(QIcon(iconFolder+"/startPause.png"));

    if(historyFile.size())
    {
        ifstream ifs(historyFile);
        if(ifs.is_open())
        {
            string line;
            while(getline(ifs,line))
            {
                if(line.empty()) continue;
                historyMenu->addAction(new SCommandAction(QString::fromStdString("MainWindow Open "+line),
                                                              QString::fromStdString(line),historyMenu));
            }
        }
    }

    operateDock   =new QDockWidget(this);
//    slamTab       =new QTabWidget(this);
    QTabWidget* operaterTab=new QTabWidget(operateDock);
    splitterLeft    =new QSplitter(Qt::Vertical,operateDock);

    operateDock->setWindowTitle("SideBar");
//    operateDock->setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea);// FIXME: Why no ok on windows qt 5.7.1
    operaterTab->addTab(splitterLeft,"FrameVis");
    ShowLayerWidget* layersWidget=new ShowLayerWidget(operateDock);
    operaterTab->addTab(layersWidget,"Layers");
    operateDock->setWidget(operaterTab);
    addDockWidget(Qt::LeftDockWidgetArea,operateDock);
    frameVis      =new FrameVisualizer(splitterLeft);
    gimageVis     =new GImageVisualizer(splitterLeft);
    win3d         =new Win3D(this);

    setCentralWidget(win3d);

    connect(openAction,SIGNAL(triggered(bool)),this,SLOT(slotOpen()));
    connect(startAction,SIGNAL(triggered(bool)),this,SLOT(slotStart()));
    connect(pauseAction,SIGNAL(triggered(bool)),this,SLOT(slotPause()));
    connect(stopAction,SIGNAL(triggered(bool)),this,SLOT(slotStop()));
    connect(oneStepAction,SIGNAL(triggered(bool)),this,SLOT(slotOneStep()));
    connect(this,SIGNAL(signalStop()),this,SLOT(slotStop()));
    return 0;
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    switch(e->key())
    {
    case Qt::Key_Space:
        break;

    case Qt::Key_Escape:
        close();
        break;

    default:
        svar.GetInt("KeyPressMsg",0) = e->key();
        break;
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
#if 0
    // 1 - left
    // 2 - right
    // 4 - middle
    printf("window pressed, %d, %d, %d\n", event->button(), event->pos().x(), event->pos().y());

    if( event->button() == 1 ) {

    }
#endif
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if(!slotStop()) return;
//    this->~MainWindow();
}

void MainWindow::call(QString cmd)
{
    emit call_signal(cmd);
}

void MainWindow::call_slot(QString cmd)
{
    if("Show"==cmd)   show();
    if("Update"==cmd)
    {
        win3d->update();
    }
    else
        svar.call(cmd.toStdString());
}

void MainWindow::slotShowMessage(QString str,int msgType)
{
    QMessageBox message(QMessageBox::NoIcon, "GSLAM",str);
    message.setIconPixmap(QIcon(":icon/rtmapper.png").pixmap(QSize(64,64)));
    message.exec();
}

bool MainWindow::slotOpen()
{
    return slotOpen("");
}

bool MainWindow::slotOpen(QString filePath)
{
    if(!filePath.size())
    {
        filePath= QFileDialog::getOpenFileName(this, tr("Choose a file.\n"),
                                                       lastOpenFile.c_str(),
                                                      "Allfile(*.*);;");
    }
    QFileInfo info(filePath);
    if(!info.exists()) return false;
    if(info.suffix()=="so"||info.suffix()=="dll"){
        return slotAddSLAM(filePath);
    }
    else return slotStartDataset(filePath);
    return true;
}

bool MainWindow::slotStart()
{
    pub_gui.publish<std::string>("Dataset Start");

    return true;
}

bool MainWindow::slotPause()
{
    pub_gui.publish<std::string>("Dataset Pause");

    return true;
}

bool MainWindow::slotOneStep()
{
    pub_gui.publish<std::string>("Dataset Step");
    return true;
}

bool MainWindow::slotStop()
{
    pub_gui.publish<std::string>("Dataset Stop");

    if(svar.GetInt("AutoClose")) close();
    return true;
}

bool MainWindow::slotAddSLAM(QString pluginPath)
{
//    SPtr<SLAMVisualizer> vis(new SLAMVisualizer(slam,dynamic_cast<GObjectHandle*>(this)));
//    slamVis.push_back(vis);
//    SLAMVisualizer* visPtr=vis.get();
//    connect(visPtr,SIGNAL(signalUpdate()),this,SLOT(slotUpdate()));
//    connect(visPtr,SIGNAL(signalSetSceneCenter(qreal,qreal,qreal)),
//            this,SLOT(slotSetSceneCenter(qreal,qreal,qreal)));
//    connect(visPtr,SIGNAL(signalSetSceneRadius(qreal)),
//            this,SLOT(slotSetSceneRadius(qreal)));
//    connect(visPtr,SIGNAL(signalSetViewPoint(qreal,qreal,qreal,qreal,qreal,qreal,qreal)),
//            this,SLOT(slotSetViewPoint(qreal,qreal,qreal,qreal,qreal,qreal,qreal)));

    return false;
}

bool MainWindow::slotStartDataset(QString datasetPath)
{
    pub_gui.publish<std::string>("Dataset Open "+datasetPath.toStdString());
    return true;
}

void MainWindow::slotUpdate(){
    win3d->updateGL();
}

void MainWindow::slotSetSceneRadius(qreal radius)
{
    win3d->setSceneRadius(radius);
}

void MainWindow::slotSetSceneCenter(qreal x,qreal y,qreal z)
{
    win3d->setSceneCenter(qglviewer::Vec(x,y,z));
}

void MainWindow::slotSetViewPoint(qreal x,qreal y,qreal z,
                        qreal rw,qreal rx,qreal ry,qreal rz)
{
    win3d->camera()->setPosition(qglviewer::Vec(x,y,z));
    win3d->camera()->setOrientation(qglviewer::Quaternion(rw,rz,-ry,-rx));
}

void MainWindow::slotDatasetStatusUpdated(int status)
{
    switch (status) {
    case DatasetPlayer::READY:{
        startAction->setEnabled(true);
        oneStepAction->setEnabled(true);
    }
        break;
    case DatasetPlayer::PLAYING:{
        startAction->setDisabled(true);
        pauseAction->setDisabled(false);
        stopAction->setDisabled(false);
        oneStepAction->setDisabled(true);
    }
        break;
    case DatasetPlayer::PAUSED:{
        startAction->setDisabled(false);
        pauseAction->setDisabled(true);
        stopAction->setDisabled(false);
        oneStepAction->setDisabled(false);
    }
        break;
    case DatasetPlayer::FINISHING:{
        slotStop();
    }
        break;
    case DatasetPlayer::FINISHED:{
        startAction->setDisabled(true);
        pauseAction->setDisabled(true);
        stopAction->setDisabled(true);
        oneStepAction->setDisabled(true);
    }
        break;
    default:
        break;
    }
}

void MainWindow::slotTryVisualize(QString topic,QString type)
{
//    LOG(INFO)<<"Try visualize "<<topic.toStdString();
    std::string typeName=type.toStdString();
    if(typeid(GSLAM::Map).name()==typeName){
        std::string topicName=topic.toStdString();
        int idx=topicName.find("/map");
        if(idx==std::string::npos) return;
        std::string slamName =topicName.substr(0,idx);
        if(slamName.empty()) return;
        if(!win3d->_objects.count(slamName)){
            std::shared_ptr<MapVisualizer> vis(new MapVisualizer(slamName,dynamic_cast<GObjectHandle*>(this)));
            win3d->_objects.insert(std::make_pair(slamName,vis));
        }
        LOG(INFO)<<"Auto visualize Map "<<topicName;
    }
    else if(typeid(GSLAM::MapFrame).name()==typeName){
        std::string topicName=topic.toStdString();
        int idx=topicName.find("/curframe");
        if(idx==std::string::npos) return;
        std::string slamName =topicName.substr(0,idx);
        if(slamName.empty()) return;
        if(!win3d->_objects.count(slamName)){
            std::shared_ptr<MapVisualizer> vis(new MapVisualizer(slamName,dynamic_cast<GObjectHandle*>(this)));
            win3d->_objects.insert(std::make_pair(slamName,vis));
        }
        LOG(INFO)<<"Auto visualize MapFrame "<<topicName;
    }
    else if(typeid(GSLAM::GImage).name()==typeName){
        std::string topicName=topic.toStdString();
        Subscriber sub=GSLAM::Messenger::instance().
                subscribe<GImage>(topicName,0,[this,topicName](const std::shared_ptr<GImage>& img){
            gimageVis->imshow(topicName,*img);
        });
        LOG(INFO)<<"Auto visualize GImage "<<topicName;
    }
}

void MainWindow::runSLAMMain()
{
//    Evaluation evaluation;
//    auto msg=GSLAM::Messenger::instance();
//    auto pub_curframe=msg.advertise<GSLAM::MapFrame>("curframe");
//    auto pub_processed_frame=msg.advertise<GSLAM::MapFrame>("processed_frame");
//    double speed=svar.GetDouble("PlaySpeed",1.);
//    double startTime=-1;
//    double& playSpeedWarningTime=svar.GetDouble("PlaySpeedWarning",5);
//    GSLAM::TicToc tictoc,tictocWarning;
//    GSLAM::FramePtr frame;

//    while(status!=STOP)
//    {
//        if(status==PAUSE)
//        {
//            GSLAM::Rate::sleep(0.001);
//            startTime=-1;
//            continue;
//        }

//        {
//            GSLAM::ScopedTimer mt("Dataset::grabFrame");
//            frame=dataset.grabFrame();
//        }

//        if(!frame) break;

//        if(startTime<0){
//            startTime=frame->timestamp();
//            tictoc.Tic();
//        }
//        else{
//            double shouldSleep=(frame->timestamp()-startTime)/speed-tictoc.Tac();
//            if(shouldSleep<-2){
//                if(tictocWarning.Tac()>playSpeedWarningTime)// Don't bother
//                {
//                    LOG(WARNING)<<"Play speed not realtime! Speed approximate "
//                               <<(frame->timestamp()-startTime)/tictoc.Tac();
//                    tictocWarning.Tic();
//                }
//            }
//            else GSLAM::Rate::sleep(shouldSleep);
//        }

//        for(auto& vis:slamVis)
//        {
//            string str=vis->slam()->type()+"::Track";
//            GSLAM::ScopedTimer mt(str.c_str());
//            pub_curframe.publish(frame);
//            vis->slam()->track(frame);
//            pub_processed_frame.publish(frame);
//        }

//        frameVis->showFrame(frame);
////        gimageVis->imshow("CurrentImage",frame->getImage());

//        if(status==ONESTEP){
//            status=PAUSE;
//        }
//    }

//    for(auto& vis:slamVis)
//    {
//        string str=vis->slam()->type()+"::Finalize";
//        GSLAM::ScopedTimer mt(str.c_str());
//        vis->slam()->finalize();
//    }
//    std::cerr<<"Play thread stoped."<<endl;
//    for(auto& vis:slamVis)
//    {
//        vis->releaseSLAM();
//    }
//    emit signalStop();
}

SCommandAction::SCommandAction(const QString &cmd, const QString &text, QMenu *parent)
    :QAction(text,(QObject*)parent),_cmd(cmd)
{
      setObjectName(text);
      if(parent)
          parent->addAction(this);
        connect(this, SIGNAL(triggered()), this, SLOT(triggerdSlot()));
//    connect(((QAction*)this), &QAction::triggered, this, SCommandAction::triggerdSlot);
}

void SCommandAction::triggerdSlot()
{
//    std::cerr<<"SCommandAction::triggerdSlot";
    svar.call(_cmd.toStdString());
}


void MainWindow::handle(const std::shared_ptr<GObject>& obj)
{
    if(auto e=dynamic_pointer_cast<DebugImageEvent>(obj)){
        gimageVis->imshow(e->_name,e->_img);
    }
    else if(auto e=dynamic_pointer_cast<ScenceCenterEvent>(obj))
    {
        Point3d c=e->_center;
        win3d->setSceneCenter(qglviewer::Vec(c.x,c.y,c.z));
    }
    else if(auto e=dynamic_pointer_cast<ScenceRadiusEvent>(obj))
    {
        win3d->setSceneRadius(e->_radius);
    }
    else win3d->update();
}

}

#endif
