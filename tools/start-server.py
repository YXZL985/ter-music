#!/usr/bin/env python3
"""
端闱乐部 微服务启动程式 — 交互式启动 SMB/FTP/SFTP/WebDAV/HTTP 服务

用法:
    python3 tools/start-server.py                           # 交互式启动
    python3 tools/start-server.py --protocol ftp [opts]     # 参数启动

依赖安装:
    pip install -r tools/requirements.txt
"""

import os
import sys
import getpass
import threading
import signal
import socket
import argparse
import http.server
import socketserver
import urllib.parse
from datetime import datetime

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
  ╭──────────────────────────────────────────────╮
  │        端闱乐部  微服务启动程式              │
  │    SMB · FTP · SFTP · WebDAV · HTTP          │
  ╰──────────────────────────────────────────────╯
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


def parse_args():
    """解析命令行参数"""
    parser = argparse.ArgumentParser(
        prog="start-server.py",
        description="端闱乐部 微服务启动程式 - 启动 SMB/FTP/SFTP/WebDAV/HTTP 服务",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 交互式启动
  python3 tools/start-server.py

  # 参数方式启动 FTP
  python3 tools/start-server.py --protocol ftp --port 2121 --share-dir ~/Music

  # 参数方式启动 WebDAV（无认证）
  python3 tools/start-server.py --protocol webdav --port 8080 --no-auth

  # 参数方式启动 SFTP
  python3 tools/start-server.py --protocol sftp --port 2222 --username test --password test

  # 参数方式启动 SMB
  python3 tools/start-server.py --protocol smb --port 445 --share-name Music --username user --password pass

  # 参数方式启动 HTTP
  python3 tools/start-server.py --protocol http --port 8088 --share-dir ~/Music

  # 参数方式启动 HTTP（带基本认证）
  python3 tools/start-server.py --protocol http --port 8088 --username user --password pass
        """
    )

    parser.add_argument(
        "--protocol", "-p",
        choices=["ftp", "webdav", "sftp", "smb", "http"],
        help="协议类型: ftp, webdav, sftp, smb, http"
    )

    parser.add_argument(
        "--host",
        default="0.0.0.0",
        help="监听地址 (默认: 0.0.0.0)"
    )

    parser.add_argument(
        "--port",
        type=int,
        help="端口号"
    )

    parser.add_argument(
        "--share-dir", "-d",
        dest="share_dir",
        help="共享目录路径"
    )

    parser.add_argument(
        "--share-name",
        help="SMB 共享名称"
    )

    parser.add_argument(
        "--username", "-u",
        help="用户名"
    )

    parser.add_argument(
        "--password", "-P",
        dest="password",
        help="密码"
    )

    parser.add_argument(
        "--sftp-authorized-keys",
        help="SFTP 公钥认证文件路径 (authorized_keys 格式)"
    )

    parser.add_argument(
        "--anonymous",
        action="store_true",
        help="允许匿名访问 (FTP/WebDAV)"
    )

    parser.add_argument(
        "--no-auth",
        action="store_true",
        help="禁用认证 (WebDAV)"
    )

    parser.add_argument(
        "--version", "-v",
        action="store_true",
        help="显示版本信息"
    )

    return parser.parse_args()


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


def start_ftp_args(args):
    """从参数启动 FTP 服务器"""
    try:
        from pyftpdlib.authorizers import DummyAuthorizer
        from pyftpdlib.handlers import FTPHandler
        from pyftpdlib.servers import FTPServer
    except ImportError:
        log_error("请安装 pyftpdlib: pip install pyftpdlib")
        sys.exit(1)

    host = args.host
    port = args.port
    share_dir = args.share_dir
    anonymous = args.anonymous

    username = None
    password = None
    if not anonymous:
        if not args.username:
            log_error("FTP 非匿名访问需要提供用户名 (--username)")
            sys.exit(1)
        username = args.username
        if not args.password:
            password = getpass.getpass("  密码: ")
        else:
            password = args.password

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
        config["middleware_stack"] = [
            "wsgidav.mw.debug_filter.WsgiDavDebugFilter",
            "wsgidav.error_printer.ErrorPrinter",
            "wsgidav.mw.cors.Cors",
            "wsgidav.request_resolver.RequestResolver",
            "wsgidav.http_authenticator.HTTPAuthenticator"
        ]
    else:
        config["middleware_stack"] = [
            "wsgidav.mw.debug_filter.WsgiDavDebugFilter",
            "wsgidav.error_printer.ErrorPrinter",
            "wsgidav.mw.cors.Cors",
            "wsgidav.request_resolver.RequestResolver"
        ]
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


def start_webdav_args(args):
    """从参数启动 WebDAV 服务器"""
    try:
        from wsgidav.wsgidav_app import WsgiDAVApp
        from wsgidav.fs_dav_provider import FilesystemProvider
        from cheroot import wsgi as cheroot_wsgi
    except ImportError:
        log_error("请安装 wsgidav: pip install wsgidav")
        sys.exit(1)

    host = args.host
    port = args.port
    share_dir = args.share_dir

    # --no-auth 优先于 --anonymous
    if args.no_auth:
        need_auth = False
    elif args.anonymous:
        need_auth = False
    else:
        # 如果提供了用户名则需要认证
        need_auth = bool(args.username)

    username = None
    password = None
    if need_auth:
        if not args.username:
            log_error("WebDAV 认证需要提供用户名 (--username)")
            sys.exit(1)
        username = args.username
        if not args.password:
            password = getpass.getpass("  密码: ") or "password"
        else:
            password = args.password

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
        config["middleware_stack"] = [
            "wsgidav.mw.debug_filter.WsgiDavDebugFilter",
            "wsgidav.error_printer.ErrorPrinter",
            "wsgidav.mw.cors.Cors",
            "wsgidav.request_resolver.RequestResolver",
            "wsgidav.http_authenticator.HTTPAuthenticator"
        ]
    else:
        config["middleware_stack"] = [
            "wsgidav.mw.debug_filter.WsgiDavDebugFilter",
            "wsgidav.error_printer.ErrorPrinter",
            "wsgidav.mw.cors.Cors",
            "wsgidav.request_resolver.RequestResolver"
        ]
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
    auth_keys_path = prompt("公钥认证文件路径 (authorized_keys，留空仅密码)", "")
    auth_keys = None
    if auth_keys_path:
        auth_keys_path = resolve_path(auth_keys_path)
        if os.path.exists(auth_keys_path):
            auth_keys = auth_keys_path
            log_step(f"已加载公钥认证文件: {auth_keys_path}")
        else:
            log_warning(f"公钥文件不存在: {auth_keys_path}")

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
        def __init__(self, username, password, authorized_keys=None):
            self._username = username
            self._password = password
            self._authorized_keys = authorized_keys
            self._allowed_keys = []
            if authorized_keys and os.path.exists(authorized_keys):
                from paramiko import RSAKey as PK_RSAKey
                with open(authorized_keys) as f:
                    for line in f:
                        line = line.strip()
                        if not line or line.startswith('#'):
                            continue
                        try:
                            k = PK_RSAKey(data=line.encode())
                            self._allowed_keys.append(k)
                        except Exception:
                            pass

        def check_auth_password(self, user, pwd):
            return (paramiko.AUTH_SUCCESSFUL
                    if user == self._username and pwd == self._password
                    else paramiko.AUTH_FAILED)

        def check_auth_publickey(self, key):
            for k in self._allowed_keys:
                if key == k:
                    return paramiko.AUTH_SUCCESSFUL
            return paramiko.AUTH_FAILED

        def get_allowed_auths(self, username):
            if self._allowed_keys:
                return "password,publickey"
            return "password"

        def check_channel_request(self, kind, chanid):
            return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

        def check_subsystem_request(self, channel, name):
            return paramiko.OPEN_SUCCEEDED if name == "sftp" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

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

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    sock.listen(5)

    def handle_conn(conn, addr):
        transport = Transport(conn)
        transport.add_server_key(host_key)
        server_iface = StubServer(username, password, auth_keys)
        sftp_iface = StubSFTPServer(server_iface)
        transport.start_server(server=server_iface)
        transport.set_subsystem_handler("sftp", paramiko.SFTPServer, sftp_iface)
        while transport.is_active():
            channel = transport.accept(10)
            if channel is None:
                continue
        transport.close()

    log_info(f"SFTP 服务器运行中，等待连接...")
    try:
        while True:
            conn, addr = sock.accept()
            t = threading.Thread(target=handle_conn, args=(conn, addr), daemon=True)
            t.start()
    except KeyboardInterrupt:
        sock.close()


def start_sftp_args(args):
    """从参数启动 SFTP 服务器"""
    try:
        import paramiko
        from paramiko import RSAKey, ServerInterface, Transport, SFTPServerInterface, SFTPServer
    except ImportError:
        log_error("请安装 paramiko: pip install paramiko")
        sys.exit(1)

    host = args.host
    port = args.port
    share_dir = args.share_dir

    if not args.username:
        log_error("SFTP 需要提供用户名 (--username)")
        sys.exit(1)
    username = args.username

    if args.password is not None:
        password = args.password
    else:
        # 后台模式下不提示输入密码，默认使用空密码
        password = ""

    auth_keys = None
    if getattr(args, 'sftp_authorized_keys', None):
        keys_path = resolve_path(args.sftp_authorized_keys)
        if os.path.exists(keys_path):
            auth_keys = keys_path
            log_step(f"已加载公钥认证文件: {keys_path}")
        else:
            log_warning(f"公钥文件不存在: {keys_path}")

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
        def __init__(self, username, password, authorized_keys=None):
            self._username = username
            self._password = password
            self._authorized_keys = authorized_keys
            self._allowed_keys = []
            if authorized_keys and os.path.exists(authorized_keys):
                from paramiko import RSAKey as PK_RSAKey
                with open(authorized_keys) as f:
                    for line in f:
                        line = line.strip()
                        if not line or line.startswith('#'):
                            continue
                        try:
                            k = PK_RSAKey(data=line.encode())
                            self._allowed_keys.append(k)
                        except Exception:
                            pass

        def check_auth_password(self, user, pwd):
            return (paramiko.AUTH_SUCCESSFUL
                    if user == self._username and pwd == self._password
                    else paramiko.AUTH_FAILED)

        def check_auth_publickey(self, key):
            for k in self._allowed_keys:
                if key == k:
                    return paramiko.AUTH_SUCCESSFUL
            return paramiko.AUTH_FAILED

        def get_allowed_auths(self, username):
            if self._allowed_keys:
                return "password,publickey"
            return "password"

        def check_channel_request(self, kind, chanid):
            return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

        def check_subsystem_request(self, channel, name):
            return paramiko.OPEN_SUCCEEDED if name == "sftp" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

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

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    sock.listen(5)

    def handle_conn(conn, addr):
        transport = Transport(conn)
        transport.add_server_key(host_key)
        server_iface = StubServer(username, password, auth_keys)
        sftp_iface = StubSFTPServer(server_iface)
        transport.start_server(server=server_iface)
        transport.set_subsystem_handler("sftp", paramiko.SFTPServer, sftp_iface)
        while transport.is_active():
            channel = transport.accept(10)
            if channel is None:
                continue
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


def start_smb_args(args):
    """从参数启动 SMB 服务器"""
    try:
        from impacket.smb import SMB
        from impacket.smbconnection import SMBConnection
        from impacket.smbserver import SimpleSMBServer
    except ImportError:
        log_error("请安装 impacket: pip install impacket")
        sys.exit(1)

    log_warn("SMB 服务器需要 root 权限（绑定 445 端口或使用 impacket）")

    host = args.host
    port = args.port
    share_name = args.share_name or "Music"
    share_dir = args.share_dir
    username = args.username or ""
    password = args.password or ""

    if username and not password:
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


# ── HTTP (nginx 风格静态文件服务器) ──────────────────────────────────────

class ThreadingHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    allow_reuse_address = True
    daemon_threads = True


class NginxStyleHTTPHandler(http.server.SimpleHTTPRequestHandler):
    """生成 nginx 风格 autoindex 页面的 HTTP 文件服务处理器"""

    def __init__(self, *args, directory=None, auth=None, **kwargs):
        self._auth = auth
        super().__init__(*args, directory=directory, **kwargs)

    def do_GET(self):
        if self._auth and not self._check_auth():
            self.send_response(401)
            self.send_header("WWW-Authenticate", 'Basic realm="Ter-Music HTTP"')
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            self.wfile.write(b"Authentication required")
            return
        super().do_GET()

    def do_HEAD(self):
        if self._auth and not self._check_auth():
            self.send_response(401)
            self.send_header("WWW-Authenticate", 'Basic realm="Ter-Music HTTP"')
            self.end_headers()
            return
        super().do_HEAD()

    def _check_auth(self):
        auth_header = self.headers.get("Authorization", "")
        if not auth_header.startswith("Basic "):
            return False
        import base64
        try:
            decoded = base64.b64decode(auth_header[6:]).decode("utf-8")
            user, _, passwd = decoded.partition(":")
            return user == self._auth[0] and passwd == self._auth[1]
        except Exception:
            return False

    def list_directory(self, path):
        """生成 nginx 风格 autoindex HTML"""
        try:
            entries = sorted(os.listdir(path), key=str.lower)
        except OSError:
            self.send_error(404, "No permission to list directory")
            return None

        display_path = urllib.parse.unquote(self.path)
        lines = []
        lines.append("<!DOCTYPE html>")
        lines.append("<html>")
        lines.append("<head>")
        lines.append(f"<title>Index of {display_path}</title>")
        lines.append("<meta charset=\"utf-8\">")
        lines.append("<style>")
        lines.append("body{font-family:sans-serif;margin:2em}")
        lines.append("a{text-decoration:none;color:#06c}")
        lines.append("a:hover{text-decoration:underline}")
        lines.append("pre{font-size:14px}")
        lines.append("</style>")
        lines.append("</head>")
        lines.append("<body>")
        lines.append(f"<h1>Index of {display_path}</h1>")
        lines.append("<hr><pre>")

        if self.path != "/":
            lines.append(f'<a href="../">../</a>')

        for name in entries:
            full_path = os.path.join(path, name)
            is_dir = os.path.isdir(full_path)
            display_name = name + "/" if is_dir else name
            href = urllib.parse.quote(name) + ("/" if is_dir else "")

            # Get file info
            try:
                st = os.stat(full_path)
                mtime = datetime.fromtimestamp(st.st_mtime).strftime("%d-%b-%Y %H:%M")
                if is_dir:
                    size = "  -"
                else:
                    size = st.st_size
                    # Human-readable size
                    for unit in ("", "K", "M", "G"):
                        if size < 1024:
                            break
                        size /= 1024
                    size = f"{size:>7.1f}{unit}" if isinstance(size, float) else f"{size:>7}"
            except OSError:
                mtime = "01-Jan-2000 00:00"
                size = "  -"

            line = f'<a href="{href}">{display_name}</a>'
            # Pad to align columns (min 50 chars for name column)
            pad = max(2, 50 - len(line))
            lines.append(f"{line}{' ' * pad}{mtime} {size}")

        lines.append("</pre><hr>")
        lines.append("</body>")
        lines.append("</html>")

        encoded = "\n".join(lines).encode("utf-8", "surrogateescape")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        return self.wfile.write(encoded)


def start_http():
    """交互式启动 HTTP 文件服务器"""
    section("HTTP 服务器配置")

    host = prompt("监听地址", "0.0.0.0")
    port = prompt_int("端口", 8088, 1, 65535)
    share_dir = resolve_path(prompt("共享目录", os.path.expanduser("~/Music")))
    need_auth = prompt_yesno("启用基本认证", False)

    auth = None
    if need_auth:
        username = prompt("用户名", "user")
        password = getpass.getpass("  密码: ") or "password"
        auth = (username, password)

    print_summary("HTTP", host, port, share_dir,
                  {"认证": f"{auth[0]}/******" if auth else "无",
                   "测试命令": f"curl http://{'localhost' if host=='0.0.0.0' else host}:{port}/"})

    def make_handler(*args, **kwargs):
        return NginxStyleHTTPHandler(*args, directory=share_dir, auth=auth, **kwargs)

    server = ThreadingHTTPServer((host, port), make_handler)
    log_info(f"HTTP 服务器运行中: http://{host}:{port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.shutdown()


def start_http_args(args):
    """从参数启动 HTTP 文件服务器"""
    host = args.host
    port = args.port
    share_dir = args.share_dir

    auth = None
    if args.username:
        password = args.password or getpass.getpass("  密码: ")
        auth = (args.username, password)

    print_summary("HTTP", host, port, share_dir,
                  {"认证": f"{auth[0]}/******" if auth else "无",
                   "测试命令": f"curl http://{'localhost' if host=='0.0.0.0' else host}:{port}/"})

    def make_handler(*args, **kwargs):
        return NginxStyleHTTPHandler(*args, directory=share_dir, auth=auth, **kwargs)

    server = ThreadingHTTPServer((host, port), make_handler)

    log_info(f"HTTP 服务器运行中: http://{host}:{port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.shutdown()


# ── 主菜单 ────────────────────────────────────────────────────────────────

def main():
    args = parse_args()

    # 显示版本
    if args.version:
        print(f"{BOLD}端闱乐部 微服务启动程式 v1.0.0{RESET}")
        print(f"Python: {sys.version}")
        sys.exit(0)

    # 如果提供了协议参数，使用参数启动
    if args.protocol:
        start_server_from_args(args)
        return

    # 否则交互式启动
    print(BANNER)
    print(f"  {DIM}选择要启动的测试服务器：{RESET}\n")
    print(f"    {BOLD}1{RESET})  FTP        (pyftpdlib)   — 端口 2121")
    print(f"    {BOLD}2{RESET})  WebDAV     (wsgidav)     — 端口 8080")
    print(f"    {BOLD}3{RESET})  SFTP       (paramiko)    — 端口 2222")
    print(f"    {BOLD}4{RESET})  SMB/CIFS   (impacket)    — 端口 445")
    print(f"    {BOLD}5{RESET})  HTTP       (stdlib)      — 端口 8088")
    print(f"    {BOLD}0{RESET})  退出\n")

    choice = prompt("请输入编号", "1")

    SERVERS = {
        "1": ("FTP", start_ftp),
        "2": ("WebDAV", start_webdav),
        "3": ("SFTP", start_sftp),
        "4": ("SMB/CIFS", start_smb),
        "5": ("HTTP", start_http),
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


def start_server_from_args(args):
    """从命令行参数启动服务器"""
    protocol = args.protocol.lower()

    # 设置默认端口
    default_ports = {"ftp": 2121, "webdav": 8080, "sftp": 2222, "smb": 445, "http": 8088}
    if args.port is None:
        args.port = default_ports.get(protocol, 0)

    # 设置默认共享目录
    if args.share_dir is None:
        args.share_dir = os.path.expanduser("~/Music")
    args.share_dir = resolve_path(args.share_dir)

    if protocol == "ftp":
        start_ftp_args(args)
    elif protocol == "webdav":
        start_webdav_args(args)
    elif protocol == "sftp":
        start_sftp_args(args)
    elif protocol == "smb":
        start_smb_args(args)
    elif protocol == "http":
        start_http_args(args)
    else:
        log_error(f"不支持的协议: {protocol}")
        sys.exit(1)


if __name__ == "__main__":
    main()
