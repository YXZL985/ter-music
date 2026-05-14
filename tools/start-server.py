#!/usr/bin/env python3
"""
Ter-Music 测试服务器工具 — 交互式启动 SMB/FTP/SFTP/WebDAV 服务

用法:
    python3 tools/start-server.py

依赖安装:
    pip install -r tools/requirements.txt
"""

import os
import sys
import getpass
import threading
import signal
import socket

# ── 样式 ──────────────────────────────────────────────────────────────────
RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
RED = "\033[31m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
CYAN = "\033[36m"

BANNER = f"""
{BOLD}{BLUE}
  ╭──────────────────────────────────────────╮
  │       Ter-Music  测试服务器工具          │
  │       SMB · FTP · SFTP · WebDAV          │
  ╰──────────────────────────────────────────╯
{RESET}"""


def log_info(msg):
    print(f"  {GREEN}▶{RESET} {msg}")


def log_warn(msg):
    print(f"  {YELLOW}⚠{RESET} {msg}")


def log_error(msg):
    print(f"  {RED}✘{RESET} {msg}")


def log_step(msg):
    print(f"  {CYAN}→{RESET} {msg}")


def section(title):
    print(f"\n  {BOLD}{title}{RESET}")
    print(f"  {'─' * 40}")


def prompt(text, default=None):
    if default is not None:
        val = input(f"  {text} [{DIM}{default}{RESET}]: ").strip()
        return val if val else default
    return input(f"  {text}: ").strip()


def prompt_int(text, default=None, minv=None, maxv=None):
    while True:
        raw = prompt(text, default)
        try:
            v = int(raw)
            if minv is not None and v < minv:
                log_warn(f"不能小于 {minv}")
                continue
            if maxv is not None and v > maxv:
                log_warn(f"不能大于 {maxv}")
                continue
            return v
        except ValueError:
            log_error("请输入数字")


def prompt_yesno(text, default=True):
    hint = "Y/n" if default else "y/N"
    val = input(f"  {text} [{hint}]: ").strip().lower()
    if not val:
        return default
    return val.startswith("y")


def resolve_path(p):
    """将 ~ 和相对路径转为绝对路径"""
    return os.path.abspath(os.path.expanduser(p))


def print_summary(proto, host, port, share_dir, extra=None):
    local_ip = _get_local_ip()
    print(f"\n  {GREEN}{'═' * 50}{RESET}")
    log_info(f"{BOLD}{proto} 服务器已启动{RESET}")
    log_info(f"   共享目录: {share_dir}")
    log_info(f"   监听地址: {host}:{port}")
    if local_ip and host in ("0.0.0.0", ""):
        log_info(f"   局域网地址: {local_ip}:{port}")
    if extra:
        for k, v in extra.items():
            log_info(f"   {k}: {v}")
    log_info(f"{DIM}按 Ctrl+C 停止服务器{RESET}")
    print(f"  {GREEN}{'═' * 50}{RESET}\n")


def _get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(0.1)
        s.connect(("10.255.255.255", 1))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return None


# ── FTP ───────────────────────────────────────────────────────────────────

def start_ftp():
    try:
        from pyftpdlib.authorizers import DummyAuthorizer
        from pyftpdlib.handlers import FTPHandler
        from pyftpdlib.servers import FTPServer
    except ImportError:
        log_error("请安装 pyftpdlib: pip install pyftpdlib")
        sys.exit(1)

    section("FTP 服务器配置")

    host = prompt("监听地址", "0.0.0.0")
    port = prompt_int("端口", 2121, 1, 65535)
    share_dir = resolve_path(prompt("共享目录", os.path.expanduser("~/Music")))
    anonymous = prompt_yesno("允许匿名访问", True)

    username = None
    password = None
    if not anonymous:
        print("  设置登录凭据：")
        username = prompt("用户名")
        password = getpass.getpass("  密码: ")

    print_summary("FTP", host, port, share_dir,
                  {"匿名访问": "是" if anonymous else "否",
                   "测试命令": f"curl ftp://{host if host != '0.0.0.0' else 'localhost'}:{port}/"})

    authorizer = DummyAuthorizer()
    if anonymous:
        authorizer.add_anonymous(share_dir, perm="elradfmw")
    if username and password:
        authorizer.add_user(username, password, share_dir, perm="elradfmw")

    handler = FTPHandler
    handler.authorizer = authorizer
    handler.banner = "Ter-Music FTP Test Server"

    server = FTPServer((host, port), handler)
    server.serve_forever()


# ── WebDAV ────────────────────────────────────────────────────────────────

def start_webdav():
    try:
        from wsgidav.wsgidav_app import WsgiDAVApp
        from wsgidav.fs_dav_provider import FilesystemProvider
        from cheroot import wsgi as cheroot_wsgi
    except ImportError:
        log_error("请安装 wsgidav: pip install wsgidav")
        sys.exit(1)

    section("WebDAV 服务器配置")

    host = prompt("监听地址", "0.0.0.0")
    port = prompt_int("端口", 8080, 1, 65535)
    share_dir = resolve_path(prompt("共享目录", os.path.expanduser("~/Music")))
    need_auth = prompt_yesno("启用基本认证", True)

    username = None
    password = None
    if need_auth:
        username = prompt("用户名", "user")
        password = getpass.getpass("  密码: ") or "password"

    print_summary("WebDAV", host, port, share_dir,
                  {"认证": f"{username}/******" if need_auth else "无",
                   "测试命令": f"curl http://{'localhost' if host=='0.0.0.0' else host}:{port}/"})

    config = {
        "host": host,
        "port": port,
        "provider_mapping": {"/": FilesystemProvider(share_dir)},
        "verbose": 1,
        "logging": {"enable_loggers": []},
    }

    if need_auth:
        config["http_authenticator"] = {
            "domain_server": "Ter-Music WebDAV",
            "accept_basic": True,
            "accept_digest": False,
            "default_to_digest": False,
            "preset_domain": {username: password},
        }
        config["middleware_stack"] = ["WsgiDAVDebugFilter", "HttpAuthMiddleware", "WsgiDavApp"]
    else:
        config["simple_dc"] = {
            "user_mapping": {"*": {"/": {"readonly": False}}},
        }

    app = WsgiDAVApp(config)
    server = cheroot_wsgi.Server((host, port), app)
    log_info(f"WebDAV 运行中: http://{host}:{port}/")

    try:
        server.start()
    except KeyboardInterrupt:
        server.stop()


# ── SFTP ──────────────────────────────────────────────────────────────────

def start_sftp():
    try:
        import paramiko
        from paramiko import RSAKey, ServerInterface, Transport, SFTPServerInterface, SFTPServer
    except ImportError:
        log_error("请安装 paramiko: pip install paramiko")
        sys.exit(1)

    section("SFTP 服务器配置")

    host = prompt("监听地址", "0.0.0.0")
    port = prompt_int("端口", 2222, 1, 65535)
    share_dir = resolve_path(prompt("共享目录", os.path.expanduser("~/Music")))
    username = prompt("用户名", "test")
    password = getpass.getpass("  SFTP 密码: ") or "test"

    # 检查是否已存在主机密钥
    key_dir = os.path.expanduser("~/.ssh")
    os.makedirs(key_dir, exist_ok=True)
    host_key_path = os.path.join(key_dir, "ter-music_sftp_rsa")
    import subprocess
    if not os.path.exists(host_key_path):
        log_step(f"生成 RSA 主机密钥: {host_key_path}")
        subprocess.run(
            ["ssh-keygen", "-t", "rsa", "-b", "2048", "-f", host_key_path, "-N", ""],
            capture_output=True,
        )

    print_summary("SFTP", host, port, share_dir,
                  {"用户名": username, "测试命令": f"sftp -P {port} {username}@{host if host != '0.0.0.0' else 'localhost'}:{share_dir}"})

    host_key = RSAKey(filename=host_key_path)

    class StubServer(ServerInterface):
        def __init__(self, username, password):
            self._username = username
            self._password = password

        def check_auth_password(self, user, pwd):
            return (paramiko.AUTH_SUCCESSFUL
                    if user == self._username and pwd == self._password
                    else paramiko.AUTH_FAILED)

        def check_auth_publickey(self, key):
            return paramiko.AUTH_FAILED

        def get_allowed_auths(self, username):
            return "password"

        def check_channel_request(self, kind, chanid):
            return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    class StubSFTPServer(SFTPServerInterface):
        def __init__(self, server, *args, **kwargs):
            super().__init__(server, *args, **kwargs)
            self._root = share_dir

        def _realpath(self, path):
            rel = os.path.relpath(path, "/") if os.path.isabs(path) else path
            return os.path.normpath(os.path.join(self._root, rel))

        def open(self, path, flags, attr):
            real = self._realpath(path)
            if not real.startswith(os.path.realpath(self._root)):
                return paramiko.SFTP_PERMISSION_DENIED
            try:
                fd = os.open(real, flags)
            except OSError as e:
                return paramiko.SFTP_FAILURE
            return paramiko.SFTPHandle(fd)

        def list_folder(self, path):
            real = self._realpath(path)
            try:
                entries = []
                for name in os.listdir(real):
                    st = os.lstat(os.path.join(real, name))
                    attr = paramiko.SFTPAttributes.from_stat(st, name)
                    entries.append(attr)
                return entries
            except OSError:
                return paramiko.SFTP_FAILURE

        def stat(self, path):
            real = self._realpath(path)
            try:
                st = os.lstat(real)
                return paramiko.SFTPAttributes.from_stat(st, os.path.basename(real))
            except OSError:
                return paramiko.SFTP_FAILURE

        def lstat(self, path):
            return self.stat(path)

        def remove(self, path):
            real = self._realpath(path)
            try:
                os.remove(real)
                return paramiko.SFTP_OK
            except OSError:
                return paramiko.SFTP_FAILURE

        def rename(self, oldpath, newpath):
            old = self._realpath(oldpath)
            new = self._realpath(newpath)
            try:
                os.rename(old, new)
                return paramiko.SFTP_OK
            except OSError:
                return paramiko.SFTP_FAILURE

        def mkdir(self, path, mode):
            real = self._realpath(path)
            try:
                os.mkdir(real, mode)
                return paramiko.SFTP_OK
            except OSError:
                return paramiko.SFTP_FAILURE

        def rmdir(self, path):
            real = self._realpath(path)
            try:
                os.rmdir(real)
                return paramiko.SFTP_OK
            except OSError:
                return paramiko.SFTP_FAILURE

        def chattr(self, path, attr):
            real = self._realpath(path)
            try:
                if attr.size is not None and attr.size >= 0:
                    with open(real, "ab") as f:
                        f.truncate(attr.size)
                if attr.uid is not None or attr.gid is not None:
                    os.chown(real, attr.uid or -1, attr.gid or -1)
                if attr.permissions is not None:
                    os.chmod(real, attr.permissions)
                return paramiko.SFTP_OK
            except OSError:
                return paramiko.SFTP_FAILURE

        def canonicalize(self, path):
            return self._realpath(path)

    class SFTPServer(Transport):
        def start_server(self, event=None, server=None):
            super().start_server(event, server)
            self._sftp_interface = StubSFTPServer(self)
            SFTPServer.set_subsystem_handler(self, "sftp", paramiko.SFTPServer, StubSFTPServer)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    sock.listen(5)

    def handle_conn(conn, addr):
        transport = Transport(conn)
        transport.add_server_key(host_key)
        server_iface = StubServer(username, password)
        transport.start_server(server=server_iface)
        while transport.is_active():
            channel = transport.accept(10)
            if channel is None:
                continue
            try:
                SFTPServer(transport).start_server()
            except Exception:
                pass
        transport.close()

    log_info(f"SFTP 服务器运行中，等待连接...")
    try:
        while True:
            conn, addr = sock.accept()
            t = threading.Thread(target=handle_conn, args=(conn, addr), daemon=True)
            t.start()
    except KeyboardInterrupt:
        sock.close()


# ── SMB ───────────────────────────────────────────────────────────────────

def start_smb():
    try:
        from impacket.smb import SMB
        from impacket.smbconnection import SMBConnection
        from impacket.smbserver import SimpleSMBServer
    except ImportError:
        log_error("请安装 impacket: pip install impacket")
        sys.exit(1)

    section("SMB 服务器配置")

    log_warn("SMB 服务器需要 root 权限（绑定 445 端口或使用 impacket）")

    host = prompt("监听地址", "0.0.0.0")
    port = prompt_int("SMB 端口", 445, 1, 65535)
    share_name = prompt("共享名称", "Music")
    share_dir = resolve_path(prompt("共享目录", os.path.expanduser("~/Music")))
    username = prompt("用户名（留空=匿名）", "")
    password = ""
    if username:
        password = getpass.getpass("  密码: ")

    print_summary("SMB", host, port, share_dir,
                  {"共享名": share_name,
                   "用户名": username or "(匿名)",
                   "测试命令": f"smbclient //{'localhost' if host=='0.0.0.0' else host}/{share_name} -U {username or '%'}"})

    server = SimpleSMBServer(listenAddress=host, listenPort=port)
    server.setSMB2Support(True)
    server.addShare(share_name, share_dir)

    if username and password:
        server.addCredential(username, password)
        server.setAccountFile(os.devnull)
    else:
        server.setAnonymousAccess()

    server.setSMBChallenge("TERMUSICTEST")
    server.setLogFile(None)

    log_info(f"SMB 服务器运行中...")
    try:
        server.start()
    except PermissionError:
        log_error("端口 445 需要 root 权限，尝试使用较高端口或 sudo 运行")
        sys.exit(1)
    except KeyboardInterrupt:
        server.stop()


# ── 主菜单 ────────────────────────────────────────────────────────────────

def main():
    print(BANNER)
    print(f"  {DIM}选择要启动的测试服务器：{RESET}\n")
    print(f"    {BOLD}1{RESET})  FTP        (pyftpdlib)   — 端口 2121")
    print(f"    {BOLD}2{RESET})  WebDAV     (wsgidav)     — 端口 8080")
    print(f"    {BOLD}3{RESET})  SFTP       (paramiko)    — 端口 2222")
    print(f"    {BOLD}4{RESET})  SMB/CIFS   (impacket)    — 端口 445")
    print(f"    {BOLD}0{RESET})  退出\n")

    choice = prompt("请输入编号", "1")

    SERVERS = {
        "1": ("FTP", start_ftp),
        "2": ("WebDAV", start_webdav),
        "3": ("SFTP", start_sftp),
        "4": ("SMB/CIFS", start_smb),
    }

    if choice == "0":
        print()
        log_info("再见")
        sys.exit(0)

    if choice not in SERVERS:
        log_error("无效选择")
        sys.exit(1)

    name, func = SERVERS[choice]

    print()
    log_info(f"启动 {name} 服务器...")
    print()

    try:
        func()
    except KeyboardInterrupt:
        print()
        log_info(f"{name} 服务器已停止")
    except ImportError as e:
        log_error(f"缺少依赖: {e}")
        log_info(f"请运行: pip install -r tools/requirements.txt")
        sys.exit(1)


if __name__ == "__main__":
    main()
