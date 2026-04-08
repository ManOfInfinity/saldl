async def infidl(uri: Union[str, list[str]], out: Union[Path, str], headers: Optional[CaseInsensitiveDict] = None,
                proxy: Optional[str] = None) -> None:
    out = Path(out)

    if headers:
        headers.update({k: v for k, v in headers.items() if k.lower() != "accept-encoding"})

    executable = shutil.which("infidl")
    if not executable:
        raise EnvironmentError("infidl executable not found...")

    segmented = isinstance(uri, list)
    segments_dir = out.with_name(out.name + "_segments")

    if segmented:
        input_data = "\n".join([
            f"{url}\n"
            f"\tdir={segments_dir}\n"
            f"\tout={i:08}.mp4"
            for i, url in enumerate(uri)
        ])

        arguments = [
            executable,
            "--skip-TLS-verification",
            "-c32",
            "-i-",
        ]

        for header, value in (headers or {}).items():
            arguments.extend(["-H", f"{header}: {value}"])

        if proxy:
            arguments.extend(["--proxy", proxy])

        try:
            subprocess.run(
                arguments,
                input=input_data,
                encoding="utf8",
                check=True
            )
        except subprocess.CalledProcessError:
            raise ValueError("infidl segmented download failed, aborting")

        # merge the segments together
        with open(out, "wb") as f:
            for file in sorted(segments_dir.iterdir()):
                data = file.read_bytes()
                # Apple TV+ needs this done to fix audio decryption
                data = re.sub(
                    b"(tfhd\x00\x02\x00\x1a\x00\x00\x00\x01\x00\x00\x00)\x02",
                    b"\\g<1>\x01",
                    data
                )
                f.write(data)
                file.unlink()
        segments_dir.rmdir()

    else:
        # Single file download
        arguments = [
            executable,
            "--show-details",
            "--skip-TLS-verification",
            "--resume",
            "--merge-in-order",
            "-c32",
            "--auto-size", "4",
            "-D", str(out.parent),
            "-o", out.name,
        ]

        for header, value in (headers or {}).items():
            arguments.extend(["-H", f"{header}: {value}"])

        if proxy:
            arguments.extend(["--proxy", proxy])

        arguments.append(uri)

        try:
            subprocess.run(arguments, check=True)
        except subprocess.CalledProcessError:
            raise ValueError("infidl download failed, aborting")

    print()
