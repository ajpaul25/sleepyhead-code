/*

SleepLib Fisher & Paykel Icon Loader Implementation

Author: Mark Watkins <jedimark64@users.sourceforge.net>
License: GPL
Copyright: (c)2012 Mark Watkins

*/

#include <QDir>
#include <QProgressBar>
#include <QMessageBox>
#include <QDataStream>

#include "icon_loader.h"

extern QProgressBar *qprogress;

FPIcon::FPIcon(Profile *p,MachineID id)
    :CPAP(p,id)
{
    m_class=fpicon_class_name;
    properties[STR_PROP_Brand]="Fisher & Paykel";
    properties[STR_PROP_Model]=STR_MACH_FPIcon;
}

FPIcon::~FPIcon()
{
}

FPIconLoader::FPIconLoader()
{
    m_buffer=NULL;
}

FPIconLoader::~FPIconLoader()
{
    for (QHash<QString,Machine *>::iterator i=MachList.begin(); i!=MachList.end(); i++) {
        delete i.value();
    }
}

int FPIconLoader::Open(QString & path,Profile *profile)
{
    QString newpath;

    if (path.endsWith("/"))
        path.chop(1);

    QString dirtag="FPHCARE";
    if (path.endsWith(QDir::separator()+dirtag)) {
        newpath=path;
    } else {
        newpath=path+QDir::separator()+dirtag;
    }

    newpath+="/ICON/";

    QString filename;

    QDir dir(newpath);

    if ((!dir.exists() || !dir.isReadable()))
        return 0;

    dir.setFilter(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    dir.setSorting(QDir::Name);
    QFileInfoList flist=dir.entryInfoList();

    QStringList SerialNumbers;

    bool ok;
    for (int i=0;i<flist.size();i++) {
        QFileInfo fi=flist.at(i);
        QString filename=fi.fileName();

        filename.toInt(&ok);
        if (ok) {
            SerialNumbers.push_back(filename);
        }
    }

    Machine *m;

    QString npath;
    for (int i=0;i<SerialNumbers.size();i++) {
        QString & sn=SerialNumbers[i];
        m=CreateMachine(sn,profile);

        npath=newpath+"/"+sn;
        try {
            if (m) OpenMachine(m,npath,profile);
        } catch(OneTypePerDay e) {
            profile->DelMachine(m);
            MachList.erase(MachList.find(sn));
            QMessageBox::warning(NULL,"Import Error","This Machine Record cannot be imported in this profile.\nThe Day records overlap with already existing content.",QMessageBox::Ok);
            delete m;
        }
    }
    return MachList.size();
}

int FPIconLoader::OpenMachine(Machine *mach, QString & path, Profile * profile)
{
    qDebug() << "Opening FPIcon " << path;
    QDir dir(path);
    if (!dir.exists() || (!dir.isReadable()))
         return false;

    dir.setFilter(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    dir.setSorting(QDir::Name);
    QFileInfoList flist=dir.entryInfoList();

    QString filename,fpath;
    if (qprogress) qprogress->setValue(0);

    QStringList summary, log, flw, det;

    for (int i=0;i<flist.size();i++) {
        QFileInfo fi=flist.at(i);
        filename=fi.fileName();
        fpath=path+"/"+filename;
        if (filename.left(3).toUpper()=="SUM") {
            summary.push_back(fpath);
            OpenSummary(mach,fpath,profile);
        } else if (filename.left(3).toUpper()=="DET") {
            det.push_back(fpath);
        } else if (filename.left(3).toUpper()=="FLW") {
            flw.push_back(fpath);
        } else if (filename.left(3).toUpper()=="LOG") {
            log.push_back(fpath);
        }
    }
    for (int i=0;i<det.size();i++) {
        OpenDetail(mach,det[i],profile);
    }
    for (int i=0;i<flw.size();i++) {
        OpenFLW(mach,flw[i],profile);
    }
    mach->Save();

    return true;
}


bool FPIconLoader::OpenFLW(Machine * mach,QString filename, Profile * profile)
{
    qDebug() << filename;
    QByteArray header;
    QFile file(filename);
    if (!file.open(QFile::ReadOnly)) {
        qDebug() << "Couldn't open" << filename;
        return false;
    }
    header=file.read(0x200);
    if (header.size()!=0x200) {
        qDebug() << "Short file" << filename;
        return false;
    }
    unsigned char hsum=0xff;
    for (int i=0;i<0x1ff;i++) {
        hsum+=header[i];
    }
    if (hsum!=header[0x1ff]) {
        qDebug() << "Header checksum mismatch" << filename;
    }

    QByteArray data;
    data=file.readAll();
    QDataStream in(data);
    in.setVersion(QDataStream::Qt_4_6);
    in.setByteOrder(QDataStream::LittleEndian);

    quint16 t1;
    quint32 ts;
    double ti;
    qint8 b;
    //QByteArray line;

    unsigned char * buf=(unsigned char *)data.data();

    unsigned char * endbuf=buf+data.size();

    EventList * flow=NULL;
//    qint16 dat[0x32];
    QDateTime datetime;
  //  quint8 a1,a2;

    int month,day,year,hour,minute,second;

    long pos=0;

    t1=buf[pos+1] << 8 | buf[pos];
    pos+=2;
    buf+=2;

    if (t1==0xfafe)  // End of file marker..
    {
        qDebug() << "FaFE observed in" << filename;

    }

    day=t1 & 0x1f;
    month=(t1 >> 5) & 0x0f;
    year=2000+((t1 >> 9) & 0x7f);

    //in >> a1;
    //in >> a2;
    t1=buf[pos+1] << 8 | buf[pos];
    pos+=2;
    buf+=2;

    second=(t1 & 0x1f) * 2;
    minute=(t1 >> 5) & 0x3f;
    hour=(t1 >> 11) & 0x1f;

    datetime=QDateTime(QDate(year,month,day),QTime(hour,minute,second));
    QDate date=datetime.date();

    QList<Session *> values = SessDate.values(date);
    EventStoreType pbuf[256];

    Session *sess;

    int count;
    for (int chunks=0;chunks<values.size();++chunks) { // each chunk is a seperate session
        ts=values.at(chunks)->session();

        datetime=datetime.toTimeSpec(Qt::UTC);
        QTime time=datetime.time();

        //ts=datetime.toTime_t();

        flow=NULL;
        if (Sessions.contains(ts)) {
            sess=Sessions[ts];

        } else sess=NULL;

        ti=qint64(ts)*1000L;

        // Little endian.
        // 100 byte blocks ending in 0x84 03 ?? ff ff   (900)
        // 0x90 01 ?? ff ff    (400)

        // 900 / 400   Waveform ID?
        // entire sequence ends in 0xff 7f

        count=0;
        int len;
        qint16 z1;
        qint8 z2;
        do {
            unsigned char * p=buf,*p2;

            // scan ahead to 0xffff marker
            do {
                while ((*p++ != 0xff) && (p < endbuf)) {
                    pos++;
                }
                if (p >= endbuf)
                    break;
                pos++;
            } while ((*p++ != 0xff) && (p < endbuf));
            if (p >= endbuf)
                break;
            p2=p-5;
            len=p2-buf;
            z1=p2[1] << 8 | p2[0];
            z2=p2[2];

            count++;

            double rate=1000.0/23.5;
            if (sess && !flow) {
                flow=sess->AddEventList(CPAP_FlowRate,EVL_Waveform,1.0,0,0,0,rate);
            }
            if (flow) {
                quint16 tmp;
                unsigned char * bb=(unsigned char *)buf;
                char c;
                if (len>100) {
                    int i=5;
                }

                for (int i=0;i<len/2;i++) {
                    c=bb[1];// & 0x1f;
                    //c-=0x10;
                    tmp=c << 8 | bb[0];
                    if (tmp<0) tmp=-tmp;
                    //tmp ^= 0x8000;
                    bb+=2;

                    pbuf[i]=tmp;
                }
                flow->AddWaveform(ti,pbuf,len/2,rate);
            }

            ti+=qint64(len/2)*rate;

            buf=p;

            if (buf >= endbuf-1) break;
            if ((p[0]==0xff) && (p[1]==0x7f)) {
                buf+=2;
                pos+=2;
                while ((*buf++ == 0) && (buf < endbuf)) pos++;
                break;
            }

        } while (buf < endbuf);
        if (buf >= endbuf)
            break;
    }

    return true;
}


bool FPIconLoader::OpenSummary(Machine * mach,QString filename, Profile * profile)
{
    qDebug() << filename;
    QByteArray header;
    QFile file(filename);
    if (!file.open(QFile::ReadOnly)) {
        qDebug() << "Couldn't open" << filename;
        return false;
    }
    header=file.read(0x200);
    if (header.size()!=0x200) {
        qDebug() << "Short file" << filename;
        return false;
    }
    unsigned char hsum=0xff;
    for (int i=0;i<0x1ff;i++) {
        hsum+=header[i];
    }
    if (hsum!=header[0x1ff]) {
        qDebug() << "Header checksum mismatch" << filename;
    }

    QByteArray data;
    data=file.readAll();
    long size=data.size(),pos=0;
    QDataStream in(data);
    in.setVersion(QDataStream::Qt_4_6);
    in.setByteOrder(QDataStream::LittleEndian);

    quint16 t1,t2;
    quint32 ts;
    //QByteArray line;
    unsigned char a1,a2, a3,a4, a5, p1, p2,  p3, p4, p5, j1, j2, j3 ,j4,j5,j6,j7, x1, x2;

    quint16 d1,d2,d3;


    QDateTime datetime;

    int runtime,usage;

    int day,month,year,hour,minute,second;
    QDate date;

    do {
        in >> a1;
        in >> a2;
        t1=a2 << 8 | a1;

        if (t1==0xfafe)
            break;

        day=t1 & 0x1f;
        month=(t1 >> 5) & 0x0f;
        year=2000+((t1 >> 9) & 0x7f);

        in >> a1;
        in >> a2;
        t1=a2 << 8 | a1;

        second=(t1 & 0x1f) * 2;
        minute=(t1 >> 5) & 0x3f;
        hour=(t1 >> 11) & 0x1f;

        datetime=QDateTime(QDate(year,month,day),QTime(hour,minute,second));
        date=datetime.date();
        datetime=datetime.toTimeSpec(Qt::UTC);
        ts=datetime.toTime_t();

        // the following two quite often match in value
        in >> a1;  // 0x04 Run Time
        in >> a2;  // 0x05 Usage Time
        runtime=a1 * 360; // durations are in tenth of an hour intervals
        usage=a2 * 360;

        in >> a3;  // 0x06  // Ramps???
        in >> a4;  // 0x07  // a pressure value?
        in >> a5;  // 0x08  // ?? varies.. always less than 90% leak..

        in >> d1;  // 0x09
        in >> d2;  // 0x0b
        in >> d3;  // 0x0d   // 90% Leak value..

        in >> p1;  // 0x0f
        in >> p2;  // 0x10

        in >> j1;  // 0x11
        in >> j2;  // 0x12  // Apnea Events
        in >> j3;  // 0x13  // Hypopnea events
        in >> j4;  // 0x14  // Flow Limitation events
        in >> j5;  // 0x15
        in >> j6;  // 0x16
        in >> j7;  // 0x17

        in >> p3;  // 0x18
        in >> p4;  // 0x19
        in >> p5;  // 0x1a

        in >> x1;  // 0x1b
        in >> x2;  // 0x1c    // humidifier setting

        if (!mach->SessionExists(ts)) {
            Session *sess=new Session(mach,ts);
            sess->really_set_first(qint64(ts)*1000L);
            sess->really_set_last(qint64(ts+usage)*1000L);
            sess->SetChanged(true);
            sess->setCount(CPAP_Obstructive, j2);
            sess->setCount(CPAP_Hypopnea, j3);
            SessDate.insert(date,sess);
//            sess->setCount(CPAP_Obstructive,j1);
//            sess->setCount(CPAP_Hypopnea,j2);
//            sess->setCount(CPAP_ClearAirway,j3);
//            sess->setCount(CPAP_Apnea,j4);
            //sess->setCount(CPAP_,j5);
            if (p1!=p2) {
                sess->settings[CPAP_Mode]=(int)MODE_APAP;
                sess->settings[CPAP_PressureMin]=p4/10.0;
                sess->settings[CPAP_PressureMax]=p3/10.0;
            } else {
                sess->settings[CPAP_Mode]=(int)MODE_CPAP;
                sess->settings[CPAP_Pressure]=p1/10.0;
            }
            Sessions[ts]=sess;
            mach->AddSession(sess,profile);
        }
    } while (!in.atEnd());

    return true;
}

bool FPIconLoader::OpenDetail(Machine * mach, QString filename, Profile * profile)
{
    qDebug() << filename;
    QByteArray header;
    QFile file(filename);
    if (!file.open(QFile::ReadOnly)) {
        qDebug() << "Couldn't open" << filename;
        return false;
    }
    header=file.read(0x200);
    if (header.size()!=0x200) {
        qDebug() << "Short file" << filename;
        return false;
    }
    unsigned char hsum=0;
    for (int i=0;i<0x1ff;i++) {
        hsum+=header[i];
    }
    if (hsum!=header[0x1ff]) {
        qDebug() << "Header checksum mismatch" << filename;
    }

    QByteArray index;
    index=file.read(0x800);
    long size=index.size(),pos=0;
    QDataStream in(index);

    in.setVersion(QDataStream::Qt_4_6);
    in.setByteOrder(QDataStream::LittleEndian);
    quint32 ts;
    QDateTime datetime;
    QDate date;
    QTime time;
    //FPDetIdx *idx=(FPDetIdx *)index.data();


    QVector<quint32> times;
    QVector<quint16> start;
    QVector<quint8> records;

    quint16 t1;
    quint16 strt;
    quint8 recs,z1,z2;


    int day,month,year,hour,minute,second;

    int totalrecs=0;
    do {
        in >> z1;
        in >> z2;
        t1=z2 << 8 | z1;

        if (t1==0xfafe)
            break;

        day=t1 & 0x1f;
        month=(t1 >> 5) & 0x0f;
        year=2000+((t1 >> 9) & 0x7f);

        in >> z1;
        in >> z2;
        t1=z2 << 8 | z1;

        second=(t1 & 0x1f) * 2;
        minute=(t1 >> 5) & 0x3f;
        hour=(t1 >> 11) & 0x1f;

        datetime=QDateTime(QDate(year,month,day),QTime(hour,minute,second));
        datetime=datetime.toTimeSpec(Qt::UTC);

        ts=datetime.toTime_t();

        date=datetime.date();
        time=datetime.time();
        in >> strt;
        in >> recs;
        totalrecs+=recs;
        if (Sessions.contains(ts)) {
            times.push_back(ts);
            start.push_back(strt);
            records.push_back(recs);
        }
    } while (!in.atEnd());

    QByteArray databytes=file.readAll();

    in.setVersion(QDataStream::Qt_4_6);
    in.setByteOrder(QDataStream::BigEndian);
    // 5 byte repeating patterns

    quint8 * data=(quint8 *)databytes.data();

    qint64 ti;
    quint8 pressure,leak, a1,a2,a3;
    SessionID sessid;
    Session *sess;
    int idx;
    for (int r=0;r<start.size();r++) {
        sessid=times[r];
        sess=Sessions[sessid];
        ti=qint64(sessid)*1000L;
        sess->really_set_first(ti);
        EventList * LK=sess->AddEventList(CPAP_LeakTotal,EVL_Event,1);
        EventList * PR=sess->AddEventList(CPAP_Pressure,EVL_Event,0.1);
        EventList * FLG=sess->AddEventList(CPAP_FLG,EVL_Event);
        EventList * OA=sess->AddEventList(CPAP_Obstructive,EVL_Event);
        EventList * H=sess->AddEventList(CPAP_Hypopnea,EVL_Event);
        EventList * FL=sess->AddEventList(CPAP_FlowLimit,EVL_Event);

        unsigned stidx=start[r];
        int rec=records[r];

        idx=stidx*15;
        for (int i=0;i<rec;i++) {
            for (int j=0;j<3;j++) {
                pressure=data[idx];
                leak=data[idx+1];
                a1=data[idx+2];
                a2=data[idx+3];
                a3=data[idx+4];
                PR->AddEvent(ti,pressure);
                LK->AddEvent(ti,leak);
                if (a1>0) OA->AddEvent(ti,a1);
                if (a2>0) H->AddEvent(ti,a2);
                if (a3>0) FL->AddEvent(ti,a3);
                FLG->AddEvent(ti,a3);
                ti+=120000L;
                idx+=5;
            }
        }
      //  sess->really_set_last(ti-360000L);
//        sess->SetChanged(true);
 //       mach->AddSession(sess,profile);
    }

    return 1;
}


Machine *FPIconLoader::CreateMachine(QString serial,Profile *profile)
{
    if (!profile)
        return NULL;
    qDebug() << "Create Machine " << serial;

    QList<Machine *> ml=profile->GetMachines(MT_CPAP);
    bool found=false;
    QList<Machine *>::iterator i;
    for (i=ml.begin(); i!=ml.end(); i++) {
        if (((*i)->GetClass()==fpicon_class_name) && ((*i)->properties[STR_PROP_Serial]==serial)) {
            MachList[serial]=*i;
            found=true;
            break;
        }
    }
    if (found) return *i;

    Machine *m=new FPIcon(profile,0);

    MachList[serial]=m;
    profile->AddMachine(m);

    m->properties[STR_PROP_Serial]=serial;
    m->properties[STR_PROP_DataVersion]=QString::number(fpicon_data_version);

    QString path="{"+STR_GEN_DataFolder+"}/"+m->GetClass()+"_"+serial+"/";
    m->properties[STR_PROP_Path]=path;
    m->properties[STR_PROP_BackupPath]=path+"Backup/";

    return m;
}

bool fpicon_initialized=false;
void FPIconLoader::Register()
{
    if (fpicon_initialized) return;
    qDebug() << "Registering F&P Icon Loader";
    RegisterLoader(new FPIconLoader());
    //InitModelMap();
    fpicon_initialized=true;
}