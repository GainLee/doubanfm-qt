#include "doubanplayer.h"

DoubanPlayer *DoubanPlayer::getInstance() {
    static DoubanPlayer *_instance = nullptr;
    if (_instance == nullptr)
        _instance = new DoubanPlayer(nullptr);
    return _instance;
}

DoubanPlayer::DoubanPlayer(QObject *parent) :
    QObject(parent),
    doubanfm(DoubanFM::getInstance()),
    _channel(-INT_MAX), _volume(0), _can_control(true),
    bufplaylist(nullptr)
{
    player.setPlaylist(new QMediaPlaylist(&player));
    connect(player.playlist(), SIGNAL(currentIndexChanged(int)), this, SLOT(currentIndexChanged(int)));

    connect(doubanfm, &DoubanFM::receivedNewList, [this] (const QList<DoubanFMSong>& rcvsongs) {
        this->songs = rcvsongs;
        qDebug() << "Received new playlist with" << rcvsongs.size() << "songs";
        QMediaPlaylist *playlist = player.playlist();
        playlist->clear();
        for (const DoubanFMSong& song : this->songs) {
            playlist->addMedia(QUrl(song.url));
        }
        if (player.state() != QMediaPlayer::PlayingState)
            player.play();
        setCanControl(true);
    });

    connect(doubanfm, &DoubanFM::receivedPlayingList, [this] (const QList<DoubanFMSong>& rcvsongs) {
        qDebug() << "Received new playlist with" << rcvsongs.size() << "songs";
        this->bufplaylist = new QMediaPlaylist(&player);
        bufsongs = rcvsongs;

        for (const DoubanFMSong& song : rcvsongs) {
            bufplaylist->addMedia(QUrl(song.url));
        }
        setCanControl(true);

        //if (player.state() != QMediaPlayer::PlayingState)
        //    player.play();
    });

    connect(&player, &QMediaPlayer::positionChanged, [this] (qint64 tick) { emit this->positionChanged(tick); });
    connect(doubanfm, &DoubanFM::receivedByeSong, [this] (bool suc) {
        emit this->receivedTrashSong(suc);
        setCanControl(true);
    });
    connect(doubanfm, &DoubanFM::receivedRateSong, [this] (bool suc) {
        int curIndex = player.playlist()->currentIndex();
        songs[curIndex].like = !songs[curIndex].like;
        emit this->receivedRateSong(suc);
        setCanControl(true);
    });
    connect(doubanfm, &DoubanFM::receivedSkipSong, [this] (bool suc) {
        emit this->receivedSkipSong(suc);
        setCanControl(true);
    });
    connect(&player, &QMediaPlayer::stateChanged, [this] (QMediaPlayer::State s) { emit this->stateChanged(s); });
}

void DoubanPlayer::currentIndexChanged(int position) {
    /*if (position < 0) {
        if (songs.size() > 0)
            doubanfm->getPlayingList(channel, songs.back().sid);
        else
            doubanfm->getNewPlayList(channel);
        return;
    }*/
    // Jump out of playlist
    if (position < 0) {
        if (bufplaylist == nullptr) {
            doubanfm->getNewPlayList(_channel);
            setCanControl(false);
        }
        else {
            player.playlist()->deleteLater();
            player.setPlaylist(bufplaylist);
            disconnect(player.playlist(), SIGNAL(currentIndexChanged(int)), this, SLOT(currentIndexChanged(int)));
            connect(bufplaylist, SIGNAL(currentIndexChanged(int)), this, SLOT(currentIndexChanged(int)));
            songs = bufsongs;
            bufplaylist = nullptr;
            if (player.state() != QMediaPlayer::PlayingState)
                player.play();
            player.playlist()->next();
        }
        qDebug() << Q_FUNC_INFO << "Deployed new playlist";
        return;
    }
    // Currently playing the last song in the list
    else if (position == songs.size() - 1) {
        doubanfm->getPlayingList(_channel, songs.back().sid);
    }
    // Got update playlist
    else if (bufplaylist != nullptr) {
        player.playlist()->deleteLater();
        player.setPlaylist(bufplaylist);
        disconnect(player.playlist(), SIGNAL(currentIndexChanged(int)), this, SLOT(currentIndexChanged(int)));
        connect(bufplaylist, SIGNAL(currentIndexChanged(int)), this, SLOT(currentIndexChanged(int)));
        songs = bufsongs;
        bufplaylist = nullptr;
        if (player.state() != QMediaPlayer::PlayingState)
            player.play();
        player.playlist()->next();
        qDebug() << Q_FUNC_INFO << "Got updated playlist";
        return;
    }
    emit this->currentSongChanged(songs[position]);

    qDebug() << "CurrentPlaying: ";
    qDebug() << "    artist: " << songs[position].artist;
    qDebug() << "    title: " << songs[position].title;
    qDebug() << "    album: " << songs[position].albumtitle;
    qDebug() << "    publicTime: " << songs[position].public_time;
    qDebug() << "    company: " << songs[position].company;
    qDebug() << "    kbps: " << songs[position].kbps;
    qDebug() << "    like: " << songs[position].like;
    qDebug() << "    sid: " << songs[position].sid;
    qDebug() << "    subType: " << songs[position].subtype;
}

bool DoubanPlayer::canControl() const {
    return _can_control;
}

void DoubanPlayer::setCanControl(bool can) {
    _can_control = can;
    emit canControlChanged(can);
}

void DoubanPlayer::play() {
    QPropertyAnimation *fadein = new QPropertyAnimation(&player, "volume");
    fadein->setDuration(1000);
    fadein->setStartValue(player.volume());
    player.play();
    fadein->setEndValue(_volume);
    fadein->start(QPropertyAnimation::DeleteWhenStopped);
    emit playing();

    QTime cur = QTime::currentTime();
    if (cur.msec() - lastPausedTime.msec() >= 30 * 60 * 1000) {
        doubanfm->getPlayingList(_channel, this->currentSong().sid);
        qDebug() << "Have paused for a long time, getting a new playlist";
    }
    lastPausedTime = cur;
}

void DoubanPlayer::pause() {
    emit paused();
    this->lastPausedTime = QTime::currentTime();
    QPropertyAnimation *fadeout = new QPropertyAnimation(&player, "volume");
    fadeout->setDuration(1000);
    fadeout->setStartValue(player.volume());
    _volume = player.volume();
    fadeout->setEndValue(0);
    connect(fadeout, &QPropertyAnimation::finished, [this] () {
        player.pause();
    });
    fadeout->start(QPropertyAnimation::DeleteWhenStopped);
}

DoubanFMSong DoubanPlayer::currentSong() const {
    int sindex = player.playlist()->currentIndex();
    if (sindex < 0) return DoubanFMSong();
    return songs[sindex];
}

qint64 DoubanPlayer::position() const {
    return player.position();
}

void DoubanPlayer::next() {
    int sindex = player.playlist()->currentIndex();
    if (sindex < 0) {
        doubanfm->getNewPlayList(this->_channel);
        return;
    }
    doubanfm->skipSong(songs[sindex].sid, _channel);
    player.playlist()->next();
}

void DoubanPlayer::stop() {
    this->player.stop();
    emit stopped();
}

void DoubanPlayer::rateCurrentSong() {
    int sindex = player.playlist()->currentIndex();
    doubanfm->rateSong(songs[sindex].sid, _channel, true);
    setCanControl(false);
}

void DoubanPlayer::unrateCurrentSong() {
    int sindex = player.playlist()->currentIndex();
    doubanfm->rateSong(songs[sindex].sid, _channel, false);
    setCanControl(false);
}

void DoubanPlayer::trashCurrentSong() {
    int sindex = player.playlist()->currentIndex();
    doubanfm->byeSong(songs[sindex].sid, _channel);
    setCanControl(false);
}

void DoubanPlayer::setChannel(qint32 chanid) {
    if (chanid == _channel) return;
    this->_channel = chanid;
    doubanfm->getNewPlayList(chanid);
    setCanControl(false);
}

void DoubanPlayer::setVolume(int v) {
    this->_volume = v;
    this->player.setVolume(v);
}

qint32 DoubanPlayer::channel() const {
    return this->_channel;
}

int DoubanPlayer::volume() const {
    return player.volume();
}

qint64 DoubanPlayer::duration() const {
    return player.duration();
}

QMediaPlayer::State DoubanPlayer::state() const {
    return player.state();
}